package com.james.epaperwatch

import android.annotation.SuppressLint
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.bluetooth.BluetoothGatt
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.PowerManager
import android.os.SystemClock
import android.util.Log
import com.james.epaperwatch.bridge.CalendarBridge
import com.james.epaperwatch.bridge.CallBridge
import com.james.epaperwatch.bridge.CameraRemote
import com.james.epaperwatch.bridge.FindPhoneRinger
import com.james.epaperwatch.bridge.MediaBridge
import com.james.epaperwatch.bridge.NotificationRelay
import com.james.epaperwatch.bridge.PhoneStateBridge
import com.james.epaperwatch.data.Prefs
import com.james.epaperwatch.data.WatchState
import com.james.epaperwatch.health.StepSync
import com.james.epaperwatch.link.GattLink
import com.james.epaperwatch.link.PowerPolicy
import com.james.epaperwatch.link.ReliableChannel
import com.james.epaperwatch.link.WakeAlarms
import com.james.epaperwatch.ota.FirmwareImage
import com.james.epaperwatch.ota.OtaManager
import com.james.epaperwatch.protocol.InMessage
import com.james.epaperwatch.protocol.Out
import com.james.epaperwatch.protocol.OutMessage
import com.james.epaperwatch.protocol.Protocol
import com.james.epaperwatch.ui.MainActivity

/**
 * Foreground service (type connectedDevice) that owns the BLE link while a watch is associated.
 * It runs the v2 session (HELLO, then the session-start state burst), routes watch messages to
 * the feature bridges, pushes state changes from the bridges to the watch, and hosts the
 * firmware transfer ([OtaManager]).
 *
 * State messages (TIME, SETTINGS, PHONE, NEXT_ALARM, EVENT, BUTTONS, MUSIC) are tracked per
 * key: a value counts as delivered only when the watch ACKed it, and a value that was dropped
 * (NAK, retries exhausted, queue overflow) is rebuilt from its source and sent again.
 */
class WatchLinkService : Service(), GattLink.Listener, OtaManager.Host {
    private val handler = Handler(Looper.getMainLooper())
    private lateinit var prefs: Prefs
    private lateinit var link: GattLink
    private lateinit var channel: ReliableChannel
    private lateinit var alarms: WakeAlarms
    private lateinit var phone: PhoneStateBridge
    private lateinit var calendar: CalendarBridge
    private lateinit var media: MediaBridge
    private lateinit var ringer: FindPhoneRinger
    private lateinit var ota: OtaManager

    /** True once the watch ACKed HELLO; until then only HELLO may be sent. */
    override var sessionActive = false
        private set

    /** `HELLO.flags` of the current session (0 until the watch's HELLO arrives). */
    var helloFlags = 0
        private set

    /** State key -> body the watch ACKed last in this session. */
    private val ackedState = HashMap<String, String>()
    /** State key -> newest body that is queued or in flight. */
    private val pendingState = HashMap<String, String>()
    /** State keys whose current value must be (re)sent. */
    private val dirtyState = LinkedHashSet<String>()
    /** State key -> PARSE/UNKNOWN NAKs in a row (the watch will not take that message). */
    private val hardFailures = HashMap<String, Int>()
    private val retryDirty = Runnable { flushDirtyState() }

    /** Delay before the next resend of dropped state; doubles until a resend gets through. */
    private var stateRetryMs = PowerPolicy.STATE_RETRY_MS
    /** A firmware transfer holds the connection at high priority. */
    private var otaHighPriority = false

    private val historyBuffer = ArrayList<Pair<String, Int>>()
    private var lastTimeSyncMs = 0L
    /** When this connection's secure watch first announced itself before any session (0 = not). */
    private var secureSeenAt = 0L
    private var lastSyncSaveMs = 0L
    /** Set once the service is stopping; the status notification must not be re-posted. */
    private var stopping = false

    private var otaWakeLock: PowerManager.WakeLock? = null
    private var workWakeLock: PowerManager.WakeLock? = null

    // ------------------------------------------------------------------ lifecycle

    override fun onCreate() {
        super.onCreate()
        instance = this
        prefs = Prefs(this)
        createChannel()
        goForeground()

        val power = getSystemService(PowerManager::class.java)
        otaWakeLock = power?.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "epaperwatch:ota")
            ?.apply { setReferenceCounted(false) }
        workWakeLock = power?.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "epaperwatch:work")
            ?.apply { setReferenceCounted(false) }

        alarms = WakeAlarms(
            this, listOf(ALARM_TICK, ALARM_CALENDAR, ALARM_RECONNECT), ::onWakeAlarm,
        ).also { it.register() }
        link = GattLink(
            this, handler, this,
            scheduleWake = { delay -> alarms.schedule(ALARM_RECONNECT, delay) },
            cancelWake = { alarms.cancel(ALARM_RECONNECT) },
        )
        channel = ReliableChannel(handler, ::onWatchUnresponsive, ::onChannelIdle) { bytes, done ->
            link.write(bytes, done)
        }
        ota = OtaManager(handler, this)
        phone = PhoneStateBridge(this, ::onPhoneState).also { it.register() }
        calendar = CalendarBridge(
            this, handler,
            scheduleWake = { delay -> alarms.schedule(ALARM_CALENDAR, delay) },
            cancelWake = { alarms.cancel(ALARM_CALENDAR) },
        ) { sendCalendar() }
        calendar.refreshRegistration()
        media = MediaBridge(this, handler) {
            // The bridge already decided that the watch's copy is wrong: always send.
            if (sessionActive) media.messageIfChanged()?.let { msg -> sendState(msg, force = true) }
        }
        media.start()
        ringer = FindPhoneRinger(
            this, handler,
            Intent(this, WatchLinkService::class.java).setAction(ACTION_STOP_FIND_PHONE),
        )
        ringer.restoreVolumeIfLeftRaised()
        CameraRemote.sender = { message -> if (sessionActive) send(message) }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (!goForeground()) return START_NOT_STICKY
        when (intent?.action) {
            ACTION_STOP -> {
                shutdown()
                return START_NOT_STICKY
            }
            ACTION_STOP_FIND_PHONE -> ringer.stop()
        }
        val address = prefs.watchAddress
        if (address == null) {
            shutdown()
            return START_NOT_STICKY
        }
        link.connect(address)
        // The user or a presence event asked: do not sit out a backoff (or a given-up link).
        if (intent?.action == ACTION_RECONNECT) link.kick()
        return START_STICKY
    }

    override fun onDestroy() {
        stopping = true
        instance = null
        CameraRemote.sender = null
        ota.shutdown()
        handler.removeCallbacksAndMessages(null)
        sessionActive = false
        channel.stop()
        link.shutdown()
        alarms.shutdown()
        ringer.stop()
        phone.unregister()
        calendar.stop()
        media.stop()
        releaseWakeLock(otaWakeLock)
        releaseWakeLock(workWakeLock)
        WatchState.update(
            if (prefs.isPaired) WatchState.Link.WAITING else WatchState.Link.NOT_PAIRED,
            if (prefs.isPaired) "Link service stopped" else WatchState.Link.NOT_PAIRED.label,
        )
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private fun shutdown() {
        stopping = true
        ota.shutdown()
        ringer.stop()
        link.shutdown()
        alarms.shutdown()
        stopForeground(STOP_FOREGROUND_REMOVE)
        stopSelf()
    }

    // ------------------------------------------------------------------ public API (main thread)

    /**
     * Send [message] and report on the main thread whether the watch acknowledged it:
     * `(true, null)`, or `(false, why)`. The UI uses it so that no button press is silent.
     */
    fun sendWithResult(message: OutMessage, onDone: (Boolean, String?) -> Unit) {
        if (!sessionActive) {
            onDone(false, "Watch is not connected")
            return
        }
        if (ota.blocksTraffic) {
            onDone(false, "A firmware update is running")
            return
        }
        channel.send(message) { outcome, reason ->
            val why = when (outcome) {
                ReliableChannel.Outcome.ACKED -> null
                ReliableChannel.Outcome.NAKED ->
                    "The watch refused it" + (reason?.let { " ($it)" } ?: "")
                ReliableChannel.Outcome.TIMED_OUT -> "The watch did not answer"
                ReliableChannel.Outcome.CANCELLED -> "The connection dropped before it was sent"
            }
            handler.post { onDone(why == null, why) }
        }
    }

    fun findWatch(onDone: (Boolean, String?) -> Unit = { _, _ -> }) =
        sendWithResult(Out.findWatch(), onDone)

    /** A made-up notification with an alert, to check the pop-up, tone and Messages app. */
    fun sendTestNotification(onDone: (Boolean, String?) -> Unit) {
        val time = android.text.format.DateFormat.getTimeFormat(this).format(java.util.Date())
        sendWithResult(
            Out.notify(
                TEST_NOTIFICATION_ID, "Watch companion", "Test notification",
                "Sent from the phone at $time.", alert = true,
            ),
            onDone,
        )
    }

    /** The phone's copy of the watch settings changed. */
    fun sendSettings() = sendState(settingsMessage(), force = true)

    /** The button layout changed in the app. */
    fun sendButtons() = sendState(Out.buttons(prefs.buttonLayout), force = true)

    /** Play alert tone 1..5 once on the watch. */
    fun previewTone(tone: Int) {
        if (tone in 1..5) send(Out.tonePreview(tone))
    }

    fun requestStatusAndHistory(onDone: (Boolean, String?) -> Unit = { _, _ -> }) {
        send(Out.request("STATUS"))
        sendWithResult(Out.request("HISTORY"), onDone)
    }

    /** Re-send the full session state; [onDone] fires when the watch has taken the last of it. */
    fun resync(onDone: (Boolean, String?) -> Unit = { _, _ -> }) {
        if (sessionActive && !ota.blocksTraffic) sendSessionState()
        sendWithResult(Out.request("STATUS"), onDone)
    }

    fun onCalendarSettingChanged() {
        calendar.refreshRegistration()
        sendCalendar()
    }

    fun onNotificationAccessGranted() {
        media.start()
    }

    fun sendMessage(message: OutMessage) = send(message)

    /** Start a firmware transfer; returns null if it started, else the reason it did not. */
    fun startOta(image: FirmwareImage): String? = ota.start(image)

    /** Cancel a running transfer, or dismiss a finished result. */
    fun cancelOta() = ota.cancel()

    // ------------------------------------------------------------------ OtaManager.Host

    override val otaSupported: Boolean
        get() = sessionActive && helloFlags and Protocol.FLAG_OTA != 0 && link.hasOtaCharacteristic

    override val mtu: Int get() = link.mtu

    override fun sendControl(message: OutMessage, result: ReliableChannel.Result?) =
        channel.send(message, result)

    override fun writeChunk(packet: ByteArray, done: (Boolean) -> Unit) = link.writeOta(packet, done)

    /**
     * High connection priority only while firmware data flows. Afterwards balanced, for the
     * resync that follows a failed transfer; [onChannelIdle] then drops to low power.
     */
    override fun setHighPriority(high: Boolean) {
        otaHighPriority = high
        link.requestPriority(
            if (high) BluetoothGatt.CONNECTION_PRIORITY_HIGH else BluetoothGatt.CONNECTION_PRIORITY_BALANCED,
        )
    }

    /**
     * A partial wake lock for the whole transfer and install (the transfer is a chain of
     * callbacks the CPU would otherwise sleep through); released on every exit path of
     * [OtaManager] and capped by a timeout.
     */
    override fun holdWakeLock(hold: Boolean) {
        val lock = otaWakeLock ?: return
        try {
            if (hold) lock.acquire(OtaManager.WAKE_LOCK_LIMIT_MS) else releaseWakeLock(lock)
        } catch (e: RuntimeException) {
            Log.w(TAG, "Wake lock", e)
        }
    }

    /** Everything that was held back during the transfer: send the latest values now. */
    override fun onTrafficResumed() {
        if (sessionActive) sendSessionState()
    }

    // ------------------------------------------------------------------ link callbacks

    override fun onLinkStatus(status: GattLink.Status, detail: String) {
        val state = when (status) {
            GattLink.Status.IDLE ->
                if (prefs.isPaired) WatchState.Link.WAITING else WatchState.Link.NOT_PAIRED
            GattLink.Status.BLUETOOTH_OFF -> WatchState.Link.BLUETOOTH_OFF
            GattLink.Status.NO_PERMISSION -> WatchState.Link.NO_PERMISSION
            GattLink.Status.WAITING -> WatchState.Link.WAITING
            GattLink.Status.SETTING_UP -> WatchState.Link.CONNECTING
            GattLink.Status.READY -> WatchState.Link.HANDSHAKE
            GattLink.Status.PAIRING_FAILED -> WatchState.Link.PAIRING_FAILED
        }
        setState(state, if (status == GattLink.Status.READY && detail == "Connected") "Handshaking" else detail)
    }

    override fun onReady() {
        secureSeenAt = 0L
        channel.start()
        sendHello()
    }

    /**
     * HELLO, repeated while a secure watch is being paired. Such a watch ignores every write
     * until the link is encrypted (no ACK, no NAK) and only repeats its own HELLO with flag 1,
     * so an un-ACKed HELLO is not a dead link then: keep trying for [PAIRING_WINDOW_MS].
     */
    private fun sendHello() {
        channel.send(Out.hello(appVersion())) { outcome, reason ->
            when (outcome) {
                ReliableChannel.Outcome.ACKED -> startSession()
                ReliableChannel.Outcome.NAKED -> {
                    setState(WatchState.Link.INCOMPATIBLE, "Watch rejected HELLO ($reason)")
                    link.restart("the watch rejected HELLO ($reason)")
                }
                ReliableChannel.Outcome.TIMED_OUT -> {
                    val seen = secureSeenAt
                    if (seen == 0L) {
                        setState(WatchState.Link.CONNECTING, "Watch did not answer HELLO")
                        link.restart("the watch did not answer HELLO")
                    } else if (SystemClock.elapsedRealtime() - seen < PAIRING_WINDOW_MS) {
                        handler.post { if (link.status == GattLink.Status.READY && !sessionActive) sendHello() }
                    } else {
                        // Pairing never completed, or one side lost the other side's key.
                        link.failPairing()
                    }
                }
                ReliableChannel.Outcome.CANCELLED -> Unit
            }
        }
    }

    override fun onDisconnected() {
        sessionActive = false
        secureSeenAt = 0L
        helloFlags = 0
        channel.stop()
        ota.onLinkLost()
        clearStateTracking()
        historyBuffer.clear()
        otaHighPriority = false
        alarms.cancel(ALARM_TICK)
        alarms.cancel(ALARM_CALENDAR)
        releaseWorkLockIfIdle()
    }

    /** Three messages in a row were dropped for timeout: the watch is gone, start over. */
    private fun onWatchUnresponsive() {
        handler.post {
            if (!sessionActive) return@post
            Log.w(TAG, "Watch stopped acknowledging; restarting the link")
            setState(WatchState.Link.CONNECTING, "Watch stopped responding, reconnecting")
            // Ends the session (onDisconnected) and reconnects with backoff, or gives up.
            link.restart("the watch stopped responding")
        }
    }

    /**
     * Nothing is left to send. Once the session-start burst (or any later exchange) has been
     * acknowledged the connection goes to low power: a long connection interval is what keeps
     * the watch's idle radio current down. Messages still get through, only a little slower.
     */
    private fun onChannelIdle() {
        if (sessionActive && !otaHighPriority && !ota.blocksTraffic) {
            link.requestPriority(BluetoothGatt.CONNECTION_PRIORITY_LOW_POWER)
        }
        releaseWorkLockIfIdle()
    }

    override fun onMessage(bytes: ByteArray) {
        val m = InMessage.parse(bytes) ?: return
        when (m.type) {
            "ACK" -> {
                channel.onAck(m.int(0))
                markSynced(force = false)
            }
            "NAK" -> channel.onNak(m.int(0), m.str(1))
            "HELLO" -> {
                prefs.watchProto = m.int(0)
                prefs.firmware = m.str(1)
                helloFlags = m.int(2)
                prefs.watchFlags = helloFlags
                if (m.int(0) != Protocol.VERSION) {
                    setState(WatchState.Link.INCOMPATIBLE, "Watch speaks protocol v${m.str(0)}")
                } else {
                    // Before the session exists this is the unsolicited HELLO of a secure
                    // watch that is ignoring us until the link is encrypted: bond now.
                    if (helloFlags and Protocol.FLAG_SECURE != 0 && !sessionActive) {
                        if (secureSeenAt == 0L) secureSeenAt = SystemClock.elapsedRealtime()
                        link.ensureBonded()
                    }
                    WatchState.notifyChanged()
                }
            }
            "STATUS" -> {
                prefs.watchBattery = m.int(0)
                prefs.watchMillivolts = m.int(1)
                prefs.watchSteps = m.int(2)
                prefs.watchCharging = if (m.str(3).isEmpty()) -1 else m.int(3)
                markSynced(force = true)
                if (prefs.healthSync) StepSync.onStepsToday(this, m.int(2))
            }
            "HISTORY" -> {
                val day = m.str(0)
                if (day.length == 8) historyBuffer += day to m.int(1)
            }
            "HISTORY_END" -> {
                if (historyBuffer.isNotEmpty()) {
                    val days = historyBuffer.toList()
                    prefs.history = days
                    if (prefs.healthSync) StepSync.onHistory(this, days)
                }
                historyBuffer.clear()
                WatchState.notifyChanged()
            }
            "NOTIFY_DISMISS" -> NotificationRelay.dismissFromWatch(m.int(0))
            "MEDIA" -> media.handleAction(m.str(0))
            "CALL_REJECT" -> CallBridge.reject(this)
            "FIND_PHONE" -> if (m.int(0) == 1) ringer.start() else ringer.stop()
            "CAMERA" -> when (m.str(0)) {
                "OPEN" -> CameraRemote.open(this)
                "SHUTTER" -> CameraRemote.shutter(m.int(1))
                "CLOSE" -> CameraRemote.close()
                else -> Log.w(TAG, "Unknown camera action: $m")
            }
            "SETTINGS" -> {
                prefs.vibration = m.int(0) == 1
                prefs.quiet = m.int(1) == 1
                if (m.has(2)) prefs.tone = m.int(2).coerceIn(0, 5)
                WatchState.notifyChanged()
            }
            "OTA_STATE" -> ota.onOtaState(m)
            else -> Log.w(TAG, "Unknown watch message: $m")
        }
    }

    // ------------------------------------------------------------------ session

    private fun startSession() {
        sessionActive = true
        link.markHealthy()
        setState(WatchState.Link.CONNECTED, "Connected")
        sendSessionState()
        markSynced(force = true)
        media.start()
        alarms.schedule(ALARM_TICK, PowerPolicy.TICK_MS)
        ota.onSessionStarted()
    }

    /** PROTOCOL.md "Session start" step 3, in order. */
    private fun sendSessionState() {
        clearStateTracking()
        for (key in STATE_KEYS) currentState(key)?.let { sendState(it, force = true) }
        // Start from a clean slate so ids from an earlier session cannot linger on the watch.
        send(Out.notifyClear(0))
        NotificationRelay.snapshot(Protocol.MAX_SESSION_NOTIFICATIONS).forEach(::send)
        CallBridge.sessionMessage()?.let(::send)
        send(Out.request("HISTORY"))
    }

    private fun settingsMessage(): OutMessage =
        Out.settings(prefs.vibration, prefs.quiet, prefs.tone.takeIf { it in 0..5 })

    /** The value a state key should have on the watch right now. */
    private fun currentState(key: String): OutMessage? = when (key) {
        KEY_TIME -> phone.timeMessage()
        "SETTINGS" -> settingsMessage()
        KEY_PHONE -> phone.phoneMessage()
        "NEXT_ALARM" -> phone.alarmMessage()
        "EVENT" -> calendar.eventMessage()
        "BUTTONS" -> Out.buttons(prefs.buttonLayout)
        KEY_MUSIC -> media.currentMessage()
        else -> null
    }

    private fun sendCalendar() {
        // eventMessage() also arms the wake alarm for the event's start: only while connected.
        if (sessionActive) sendState(calendar.eventMessage())
    }

    private fun onPhoneState(kind: PhoneStateBridge.Kind) {
        when (kind) {
            PhoneStateBridge.Kind.TIME -> {
                sendState(phone.timeMessage(), force = true)
                sendState(phone.alarmMessage())
                sendCalendar()
            }
            PhoneStateBridge.Kind.PHONE -> {
                if (!sessionActive) return
                // Battery in coarse steps only: not every percent is worth waking the watch.
                val message = phone.phoneMessage()
                val shown = pendingState[KEY_PHONE] ?: ackedState[KEY_PHONE]
                if (PowerPolicy.phoneWorthSending(shown, message.body)) sendState(message)
            }
            PhoneStateBridge.Kind.ALARM -> sendState(phone.alarmMessage())
        }
    }

    /** Doze-safe timers (see [WakeAlarms]). Nothing here runs without a session. */
    private fun onWakeAlarm(tag: String) {
        if (tag == ALARM_RECONNECT) {
            link.onWake()
            return
        }
        if (!sessionActive) return
        // The alarm's own wake lock ends when this returns; the BLE writes this leads to
        // need a little longer. Released as soon as both are done.
        try {
            workWakeLock?.acquire(WORK_WAKE_MS)
        } catch (e: RuntimeException) {
            Log.w(TAG, "Wake lock", e)
        }
        when (tag) {
            ALARM_TICK -> {
                alarms.schedule(ALARM_TICK, PowerPolicy.TICK_MS)
                sendCalendar()
                if (System.currentTimeMillis() - lastTimeSyncMs > PowerPolicy.TIME_RESYNC_MS) {
                    sendState(phone.timeMessage(), force = true)
                }
            }
            ALARM_CALENDAR -> sendCalendar() // the reported event has started: move on
        }
        releaseWorkLockIfIdle()
    }

    /** Let the CPU sleep again once the alarm's BLE writes are finished. */
    private fun releaseWorkLockIfIdle() {
        if (channel.pendingCount == 0) releaseWakeLock(workWakeLock)
    }

    /** Non-state messages: sent once, dropped if there is no session or a transfer runs. */
    private fun send(message: OutMessage) {
        if (!sessionActive) return
        if (ota.blocksTraffic) {
            Log.i(TAG, "Firmware transfer running; not sending $message")
            return
        }
        channel.send(message)
    }

    /**
     * Send a state message unless the watch already has this value ([force] sends anyway).
     * "Has" means ACKed: the value is recorded in [ackedState] only when its ACK arrives, and
     * a drop marks the key dirty so [flushDirtyState] sends the then-current value again.
     */
    private fun sendState(message: OutMessage, force: Boolean = false) {
        if (!sessionActive) return
        val key = message.coalesceKey ?: message.type
        if (ota.blocksTraffic) {
            dirtyState += key // the resync after the transfer sends the latest value
            return
        }
        val body = message.body
        if (!force) {
            val pending = pendingState[key]
            if (pending != null) {
                if (pending == body) return
            } else if (ackedState[key] == body) {
                return
            }
        }
        pendingState[key] = body
        dirtyState -= key
        channel.send(message) { outcome, reason -> onStateResult(key, body, outcome, reason) }
    }

    private fun onStateResult(key: String, body: String, outcome: ReliableChannel.Outcome, reason: String?) {
        when (outcome) {
            ReliableChannel.Outcome.ACKED -> {
                ackedState[key] = body
                hardFailures.remove(key)
                if (pendingState[key] == body) pendingState.remove(key)
                if (key == KEY_TIME) lastTimeSyncMs = System.currentTimeMillis()
                // Everything is through again: the next drop starts from the short delay.
                if (dirtyState.isEmpty() && pendingState.isEmpty()) stateRetryMs = PowerPolicy.STATE_RETRY_MS
            }
            ReliableChannel.Outcome.NAKED -> onStateDropped(key, body, hard = reason != "FULL")
            ReliableChannel.Outcome.TIMED_OUT -> onStateDropped(key, body, hard = false)
            ReliableChannel.Outcome.CANCELLED ->
                // "superseded": a newer value took its place. null: the link stopped, and the
                // next session sends everything. Only an overflow is a real drop.
                if (reason == ReliableChannel.REASON_OVERFLOW) onStateDropped(key, body, hard = false)
        }
    }

    private fun onStateDropped(key: String, body: String, hard: Boolean) {
        if (pendingState[key] != body) return // a newer value is already queued behind it
        pendingState.remove(key)
        if (!sessionActive) return
        if (hard) {
            // PARSE / UNKNOWN: the watch does not accept this message. Retry a few times in
            // case the value changes, then leave the key alone until something is ACKed.
            val failures = (hardFailures[key] ?: 0) + 1
            hardFailures[key] = failures
            if (failures >= MAX_HARD_FAILURES) {
                Log.w(TAG, "Watch keeps rejecting $key; not retrying")
                return
            }
        }
        if (key == KEY_MUSIC) media.invalidate()
        dirtyState += key
        // Backs off: a watch that keeps answering FULL (or keeps losing one message) is not
        // sent the same state every few seconds for ever.
        handler.removeCallbacks(retryDirty)
        handler.postDelayed(retryDirty, stateRetryMs)
        stateRetryMs = PowerPolicy.nextBackoff(stateRetryMs, PowerPolicy.STATE_RETRY_MAX_MS)
    }

    private fun flushDirtyState() {
        if (!sessionActive || ota.blocksTraffic) return
        val keys = dirtyState.toList()
        dirtyState.clear()
        for (key in keys) currentState(key)?.let { sendState(it, force = true) }
    }

    private fun clearStateTracking() {
        handler.removeCallbacks(retryDirty)
        stateRetryMs = PowerPolicy.STATE_RETRY_MS
        ackedState.clear()
        pendingState.clear()
        dirtyState.clear()
        hardFailures.clear()
    }

    private fun markSynced(force: Boolean) {
        val now = System.currentTimeMillis()
        if (!force && now - lastSyncSaveMs < 30_000L) return
        lastSyncSaveMs = now
        prefs.lastSync = now
        WatchState.notifyChanged()
    }

    private fun releaseWakeLock(lock: PowerManager.WakeLock?) {
        try {
            if (lock != null && lock.isHeld) lock.release()
        } catch (e: RuntimeException) {
            Log.w(TAG, "Wake lock release", e)
        }
    }

    // ------------------------------------------------------------------ foreground notification

    private fun setState(state: WatchState.Link, detail: String) {
        WatchState.update(state, detail)
        // After stopForeground()/onDestroy a notify() would leave a stuck ongoing notification.
        if (stopping) return
        try {
            getSystemService(NotificationManager::class.java)
                ?.notify(NOTIFICATION_ID, buildNotification(detail))
        } catch (_: SecurityException) {
        }
    }

    private fun goForeground(): Boolean = try {
        val notification = buildNotification(WatchState.detail.ifEmpty { "Starting" })
        if (Build.VERSION.SDK_INT >= 29) {
            startForeground(
                NOTIFICATION_ID, notification,
                ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE,
            )
        } else {
            startForeground(NOTIFICATION_ID, notification)
        }
        true
    } catch (e: Exception) {
        Log.e(TAG, "Cannot enter foreground", e)
        stopSelf()
        false
    }

    private fun createChannel() {
        getSystemService(NotificationManager::class.java)?.createNotificationChannel(
            NotificationChannel(CHANNEL_ID, "Watch connection", NotificationManager.IMPORTANCE_LOW)
                .apply { setShowBadge(false) },
        )
    }

    private fun buildNotification(status: String): Notification {
        val launch = PendingIntent.getActivity(
            this, 0, Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        return Notification.Builder(this, CHANNEL_ID)
            .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
            .setContentTitle("E-Paper Watch")
            .setContentText(status)
            .setContentIntent(launch)
            .setOngoing(true)
            .setShowWhen(false)
            .build()
    }

    private fun appVersion(): String = try {
        @Suppress("DEPRECATION")
        packageManager.getPackageInfo(packageName, 0).versionName ?: "?"
    } catch (_: Exception) {
        "?"
    }

    companion object {
        private const val TAG = "WatchLinkService"
        private const val CHANNEL_ID = "watch-link"
        private const val NOTIFICATION_ID = 71
        private const val MAX_HARD_FAILURES = 3
        /** Upper bound for the work wake lock; normally released within a few seconds. */
        private const val WORK_WAKE_MS = 30_000L
        /** How long a secure watch may ignore our HELLO while the bond is created. */
        private const val PAIRING_WINDOW_MS = 35_000L
        private const val ALARM_TICK = "tick"
        private const val ALARM_CALENDAR = "calendar"
        private const val ALARM_RECONNECT = "reconnect"
        private const val KEY_TIME = "TIME"
        private const val KEY_PHONE = "PHONE"
        private const val KEY_MUSIC = "MUSIC"

        /** Far above the ids the notification relay hands out in practice. */
        private const val TEST_NOTIFICATION_ID = 65535
        private const val ACTION_STOP = "com.james.epaperwatch.STOP"
        private const val ACTION_RECONNECT = "com.james.epaperwatch.RECONNECT"
        private const val ACTION_STOP_FIND_PHONE = "com.james.epaperwatch.STOP_FIND_PHONE"

        /** State keys in PROTOCOL.md session-start order. */
        private val STATE_KEYS =
            listOf(KEY_TIME, "SETTINGS", "PHONE", "NEXT_ALARM", "EVENT", "BUTTONS", KEY_MUSIC)

        /** The running service, if any. Only touched on the main thread. */
        @SuppressLint("StaticFieldLeak")
        @Volatile
        var instance: WatchLinkService? = null
            private set

        /** Start (or poke) the link service if a watch is associated. */
        fun start(context: Context) = start(context, null)

        /**
         * Like [start], and also retry right away if the link is waiting out a reconnect
         * backoff or has stopped after repeated failures. For the user's Reconnect button and
         * for the system reporting the watch in range, not for routine pokes.
         */
        fun reconnect(context: Context) = start(context, ACTION_RECONNECT)

        private fun start(context: Context, action: String?) {
            if (!Prefs(context).isPaired) return
            // On API 34 startForeground(connectedDevice) throws without BLUETOOTH_CONNECT, and
            // a startForegroundService() that never reaches startForeground() crashes the app.
            if (!GattLink.hasConnectPermission(context)) {
                WatchState.update(WatchState.Link.NO_PERMISSION, "Nearby devices permission needed")
                return
            }
            try {
                context.startForegroundService(
                    Intent(context, WatchLinkService::class.java).setAction(action),
                )
            } catch (e: Exception) {
                // e.g. ForegroundServiceStartNotAllowedException without a CDM exemption
                Log.w(TAG, "Could not start link service", e)
            }
        }

        fun stop(context: Context) {
            instance?.shutdown() ?: context.stopService(Intent(context, WatchLinkService::class.java))
        }

        /** Send to the watch if a session is active; otherwise dropped (session start resyncs). */
        fun send(message: OutMessage) {
            val service = instance ?: return
            if (Looper.myLooper() == Looper.getMainLooper()) {
                service.send(message)
            } else {
                service.handler.post { service.send(message) }
            }
        }
    }
}
