package com.james.epaperwatch.bridge

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Build
import com.james.epaperwatch.protocol.InMessage
import com.james.epaperwatch.protocol.Out
import com.james.epaperwatch.protocol.OutMessage

/**
 * Sleep as Android wearable bridge (movement only), ported to the v2 message names.
 *
 * Sleep -> watch: CHECK_CONNECTED, START/STOP_TRACKING, SET_PAUSE, SET_SUSPENDED,
 * SET_BATCH_SIZE, START/STOP/UPDATE_ALARM, SHOW_NOTIFICATION, HINT.
 * Watch -> Sleep: SLEEP_DATA movement batches (as MAX_RAW_DATA), SNOOZE, DISMISS, PAUSE, RESUME.
 */
class SleepApiBridge(
    private val context: Context,
    private val isWatchConnected: () -> Boolean,
    private val sendToWatch: (OutMessage) -> Unit,
) : BroadcastReceiver() {
    private val movementBatch = ArrayList<Float>()
    private var requestedBatchSize = 12
    private var registered = false

    fun register() {
        if (registered) return
        val filter = IntentFilter().apply {
            addAction(CHECK_CONNECTED)
            addAction(START_TRACKING)
            addAction(STOP_TRACKING)
            addAction(SET_PAUSE)
            addAction(SET_SUSPENDED)
            addAction(SET_BATCH_SIZE)
            addAction(START_ALARM)
            addAction(STOP_ALARM)
            addAction(UPDATE_ALARM)
            addAction(SHOW_NOTIFICATION)
            addAction(HINT)
        }
        // Sleep as Android is another app, so this receiver must be exported.
        if (Build.VERSION.SDK_INT >= 33) {
            context.registerReceiver(this, filter, Context.RECEIVER_EXPORTED)
        } else {
            @Suppress("UnspecifiedRegisterReceiverFlag")
            context.registerReceiver(this, filter)
        }
        registered = true
    }

    fun unregister() {
        if (!registered) return
        context.unregisterReceiver(this)
        registered = false
    }

    override fun onReceive(receiverContext: Context, intent: Intent) {
        when (intent.action) {
            CHECK_CONNECTED -> if (isWatchConnected()) sendToSleep(Intent(CONFIRM_CONNECTED))
            START_TRACKING -> {
                movementBatch.clear()
                sendToWatch(Out.sleepStart())
            }
            STOP_TRACKING -> {
                flushMovement()
                sendToWatch(Out.sleepStop())
            }
            SET_PAUSE -> {
                // TIMESTAMP is the epoch-ms the pause ends (0 = resume); the wire wants seconds.
                val until = intent.getLongExtra("TIMESTAMP", 0L)
                val seconds = if (until <= 0L) 0L else
                    ((until - System.currentTimeMillis()) / 1000L).coerceAtLeast(0L)
                sendToWatch(Out.sleepPause(seconds))
            }
            SET_SUSPENDED -> {
                val suspended = intent.getBooleanExtra("SUSPENDED", false)
                sendToWatch(Out.sleepPause(if (suspended) SUSPEND_SECONDS else 0L))
            }
            SET_BATCH_SIZE -> {
                requestedBatchSize = intent.getLongExtra("SIZE", 12L).toInt().coerceIn(1, 120)
                if (movementBatch.size >= requestedBatchSize) flushMovement()
            }
            START_ALARM -> {
                // DELAY is in milliseconds; a negative delay means "no alarm vibration".
                val delayMs = intent.getIntExtra("DELAY", 0)
                if (delayMs >= 0) sendToWatch(Out.sleepAlarm((delayMs + 999L) / 1000L))
            }
            STOP_ALARM -> sendToWatch(Out.sleepAlarmStop())
            UPDATE_ALARM -> {
                val timestamp = intent.getLongExtra("TIMESTAMP", 0L)
                if (timestamp > 0L) {
                    val delayMs = (timestamp - System.currentTimeMillis()).coerceAtLeast(0L)
                    sendToWatch(Out.sleepAlarm((delayMs + 999L) / 1000L))
                }
            }
            SHOW_NOTIFICATION -> sendToWatch(
                Out.sleepNotify(intent.getStringExtra("TITLE"), intent.getStringExtra("TEXT")),
            )
            HINT -> sendToWatch(Out.sleepHint(intent.getIntExtra("REPEAT", 1).coerceIn(1, 5)))
        }
    }

    /** Handles SLEEP_* messages from the watch. */
    fun onWatchMessage(message: InMessage) {
        when (message.type) {
            "SLEEP_DATA" -> onMovement(message.int(0))
            "SLEEP_SNOOZE" -> sendToSleep(Intent(SNOOZE_FROM_WATCH))
            "SLEEP_DISMISS" -> sendToSleep(Intent(DISMISS_FROM_WATCH))
            "SLEEP_PAUSE" -> sendToSleep(Intent(PAUSE_FROM_WATCH))
            "SLEEP_RESUME" -> sendToSleep(Intent(RESUME_FROM_WATCH))
        }
    }

    private fun onMovement(maxDeltaMg: Int) {
        // Sleep expects acceleration change in m/s². The watch sends milligravity.
        movementBatch += maxDeltaMg.coerceAtLeast(0) * 0.00980665f
        if (movementBatch.size >= requestedBatchSize) flushMovement()
    }

    private fun flushMovement() {
        if (movementBatch.isEmpty()) return
        sendToSleep(Intent(DATA_UPDATE).putExtra("MAX_RAW_DATA", movementBatch.toFloatArray()))
        movementBatch.clear()
    }

    private fun sendToSleep(intent: Intent) {
        intent.setPackage(SLEEP_PACKAGE)
        context.sendBroadcast(intent)
    }

    companion object {
        private const val SLEEP_PACKAGE = "com.urbandroid.sleep"
        private const val PREFIX = "com.urbandroid.sleep.watch."
        /** "Suspended until further notice"; SET_SUSPENDED=false sends 0 to resume. */
        private const val SUSPEND_SECONDS = 365L * 24 * 60 * 60

        const val CHECK_CONNECTED = "${PREFIX}CHECK_CONNECTED"
        const val CONFIRM_CONNECTED = "${PREFIX}CONFIRM_CONNECTED"
        const val START_TRACKING = "${PREFIX}START_TRACKING"
        const val STOP_TRACKING = "${PREFIX}STOP_TRACKING"
        const val SET_PAUSE = "${PREFIX}SET_PAUSE"
        const val SET_SUSPENDED = "${PREFIX}SET_SUSPENDED"
        const val SET_BATCH_SIZE = "${PREFIX}SET_BATCH_SIZE"
        const val START_ALARM = "${PREFIX}START_ALARM"
        const val STOP_ALARM = "${PREFIX}STOP_ALARM"
        const val UPDATE_ALARM = "${PREFIX}UPDATE_ALARM"
        const val SHOW_NOTIFICATION = "${PREFIX}SHOW_NOTIFICATION"
        const val HINT = "${PREFIX}HINT"
        const val DATA_UPDATE = "${PREFIX}DATA_UPDATE"
        const val PAUSE_FROM_WATCH = "${PREFIX}PAUSE_FROM_WATCH"
        const val RESUME_FROM_WATCH = "${PREFIX}RESUME_FROM_WATCH"
        const val SNOOZE_FROM_WATCH = "${PREFIX}SNOOZE_FROM_WATCH"
        const val DISMISS_FROM_WATCH = "${PREFIX}DISMISS_FROM_WATCH"
    }
}
