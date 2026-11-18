package com.james.epaperwatch.bridge

import android.app.Notification
import android.content.Context
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.service.notification.StatusBarNotification
import android.util.Log
import com.james.epaperwatch.WatchLinkService
import com.james.epaperwatch.WatchNotificationListener
import com.james.epaperwatch.data.Prefs
import com.james.epaperwatch.link.PowerPolicy
import com.james.epaperwatch.protocol.Out
import com.james.epaperwatch.protocol.OutMessage

/**
 * Mirrors the phone's notification shade onto the watch.
 *
 * - Filters out our own app, group summaries, ongoing / foreground-service notifications and
 *   the progress/transport/service/system/status categories, plus apps the user has denied.
 * - Only apps with a launcher icon are forwarded, so system notices (charging, USB, battery
 *   care, "running in background") never reach the watch.
 * - Each StatusBarNotification key gets a stable small id (1..65535) for as long as it is shown.
 * - Re-posts with identical (sanitised) content are not re-sent.
 * - `NOTIFY.alert` is 1 only the first time a key is forwarded from a live post; content
 *   updates of a forwarded key, session-start replays and filter re-evaluations are silent.
 * - Content updates of a forwarded key are rate-limited ([PowerPolicy.NOTIFY_UPDATE_MIN_MS]):
 *   a notification that edits itself every second reaches the watch at most once per
 *   interval, and the latest content wins.
 * - Removal sends NOTIFY_CLEAR; NOTIFY_DISMISS from the watch cancels the Android notification.
 *
 * Called on the notification listener's main thread.
 */
object NotificationRelay {
    /** [message] is the silent (alert = 0) form, used for replays and for comparing content. */
    private class Entry(val key: String, val id: Int, var message: OutMessage, var postTime: Long) {
        /** Body of the last NOTIFY handed to the link for this key (null = none yet). */
        var sentBody: String? = null
        /** When that was, on the elapsed-realtime clock (0 = never). */
        var sentAt = 0L
        /** A delayed send of the latest content is scheduled. */
        var flushScheduled = false
    }

    private val forwarded = LinkedHashMap<String, Entry>()
    private var nextId = 1
    private val handler = Handler(Looper.getMainLooper())

    /** Package -> (label, time looked up): PackageManager lookups are binder calls. */
    private val labels = HashMap<String, Pair<String, Long>>()
    private const val LABEL_TTL_MS = 6L * 60 * 60 * 1000
    private const val MAX_LABELS = 200

    /** Minimum gap between two alerting re-posts of one notification. */
    private const val UPDATE_ALERT_MIN_MS = 3_000L

    private val IGNORED_CATEGORIES = setOf(
        Notification.CATEGORY_PROGRESS,
        Notification.CATEGORY_TRANSPORT,
        Notification.CATEGORY_SERVICE,
        Notification.CATEGORY_SYSTEM,
        Notification.CATEGORY_STATUS,
    )

    /**
     * [send] false only records the notification (listener bind). [live] is true for a real
     * `onNotificationPosted`, the only case in which a new key may alert the watch.
     */
    fun onPosted(
        context: Context,
        sbn: StatusBarNotification,
        send: Boolean = true,
        live: Boolean = true,
    ) {
        try {
            post(context, sbn, send, live)
        } catch (e: RuntimeException) {
            // Another app's extras can fail to unparcel (BadParcelableException). That must not
            // crash this process, which also hosts the watch link.
            Log.w("NotificationRelay", "Ignoring notification from ${sbn.packageName}", e)
        }
    }

    private fun post(context: Context, sbn: StatusBarNotification, send: Boolean, live: Boolean) {
        if (CallBridge.onPosted(context, sbn)) return
        val message = build(context, sbn)
        val existing = forwarded[sbn.key]
        if (message == null) {
            if (existing != null) {
                forwarded.remove(sbn.key)
                if (send) WatchLinkService.send(Out.notifyClear(existing.id))
            }
            return
        }
        if (existing != null && existing.message.body == message(existing.id, message, false).body) return
        if (existing == null) {
            val id = allocateId()
            val entry = Entry(sbn.key, id, message(id, message, alert = false), sbn.postTime)
            forwarded[sbn.key] = entry
            if (!send) return
            // Alert only for a genuinely new arrival: the first time this key is forwarded.
            markSent(entry)
            WatchLinkService.send(if (live) message(id, message, alert = true) else entry.message)
            return
        }
        // New content for a notification the watch already has. The stored copy is silent
        // (it is what session-start replays send).
        existing.message = message(existing.id, message, alert = false)
        existing.postTime = sbn.postTime
        if (!send) return
        // A re-post that Android itself would alert for (a new message in an existing chat:
        // the app did not set ONLY_ALERT_ONCE) alerts the watch too, and goes out at once.
        // Limited to one alert per key every few seconds so a misbehaving app cannot buzz
        // the watch continuously; faster re-posts fall through to the silent update path.
        val wouldAlertOnPhone =
            live && (sbn.notification.flags and Notification.FLAG_ONLY_ALERT_ONCE) == 0
        if (wouldAlertOnPhone &&
            SystemClock.elapsedRealtime() - existing.sentAt >= UPDATE_ALERT_MIN_MS
        ) {
            existing.flushScheduled = false
            markSent(existing)
            WatchLinkService.send(message(existing.id, message, alert = true))
            return
        }
        val wait = if (live) {
            PowerPolicy.delayUntilAllowed(
                SystemClock.elapsedRealtime(), existing.sentAt, PowerPolicy.NOTIFY_UPDATE_MIN_MS,
            )
        } else {
            0L // a settings change (e.g. "hide text") applies at once
        }
        if (wait == 0L) {
            markSent(existing)
            WatchLinkService.send(existing.message)
        } else if (!existing.flushScheduled) {
            existing.flushScheduled = true
            val key = sbn.key
            handler.postDelayed({ flush(key) }, wait)
        }
    }

    /** The rate limit of [key] has run out: send its latest content if the watch lacks it. */
    private fun flush(key: String) {
        val entry = forwarded[key] ?: return
        if (!entry.flushScheduled) return
        entry.flushScheduled = false
        if (entry.message.body == entry.sentBody) return
        markSent(entry)
        WatchLinkService.send(entry.message)
    }

    private fun markSent(entry: Entry) {
        entry.sentBody = entry.message.body
        entry.sentAt = SystemClock.elapsedRealtime()
    }

    fun onRemoved(sbn: StatusBarNotification) {
        CallBridge.onRemoved(sbn)
        val entry = forwarded.remove(sbn.key) ?: return
        WatchLinkService.send(Out.notifyClear(entry.id))
    }

    /** Rebuild state from the currently active notifications without alerting the watch. */
    fun seed(context: Context, active: Array<StatusBarNotification>) {
        // On a listener rebind the watch still shows the earlier notifications under their
        // ids: keep the ids of survivors and clear what vanished while we were unbound.
        val activeKeys = active.map { it.key }.toSet()
        forwarded.values.filter { it.key !in activeKeys }.forEach {
            forwarded.remove(it.key)
            WatchLinkService.send(Out.notifyClear(it.id))
        }
        CallBridge.retain(activeKeys)
        active.sortedBy { it.postTime }.forEach { onPosted(context, it, send = false, live = false) }
    }

    /** Re-run filters after a settings change; sends clears/adds for whatever changed. */
    fun reevaluate(context: Context, active: Array<StatusBarNotification>) {
        val activeKeys = active.map { it.key }.toSet()
        forwarded.values.filter { it.key !in activeKeys }.forEach {
            forwarded.remove(it.key)
            WatchLinkService.send(Out.notifyClear(it.id))
        }
        active.sortedBy { it.postTime }.forEach { onPosted(context, it, send = true, live = false) }
    }

    /** The newest [max] forwarded notifications, newest last, for session start (alert = 0). */
    fun snapshot(max: Int): List<OutMessage> =
        forwarded.values.sortedBy { it.postTime }.takeLast(max).map {
            it.sentBody = it.message.body // a pending rate-limited update has nothing left to add
            it.message
        }

    fun dismissFromWatch(id: Int) {
        val entry = forwarded.values.firstOrNull { it.id == id } ?: return
        forwarded.remove(entry.key) // no echo NOTIFY_CLEAR when Android reports the removal
        WatchNotificationListener.cancel(entry.key)
    }

    // ------------------------------------------------------------------ building

    private class Content(val app: String, val title: String, val text: String)

    private fun message(id: Int, c: Content, alert: Boolean) =
        Out.notify(id, c.app, c.title, c.text, alert)

    private fun build(context: Context, sbn: StatusBarNotification): Content? {
        val prefs = Prefs(context)
        if (!prefs.forwardNotifications) return null
        val n = sbn.notification
        if (sbn.packageName == context.packageName) return null
        if (n.flags and Notification.FLAG_GROUP_SUMMARY != 0) return null
        if (n.flags and (Notification.FLAG_ONGOING_EVENT or Notification.FLAG_FOREGROUND_SERVICE) != 0) {
            return null
        }
        if (sbn.isOngoing) return null
        if (n.category in IGNORED_CATEGORIES) return null
        if (n.extras.containsKey(Notification.EXTRA_MEDIA_SESSION)) return null

        val extras = n.extras
        val title = (extras.getCharSequence(Notification.EXTRA_TITLE_BIG)
            ?: extras.getCharSequence(Notification.EXTRA_TITLE)
            ?: extras.getCharSequence(Notification.EXTRA_CONVERSATION_TITLE))?.toString().orEmpty()
        val text = (extras.getCharSequence(Notification.EXTRA_BIG_TEXT)
            ?: extras.getCharSequence(Notification.EXTRA_TEXT)
            ?: extras.getCharSequence(Notification.EXTRA_SUB_TEXT))?.toString().orEmpty()
        if (title.isBlank() && text.isBlank()) return null
        // Only apps you can open: no charging, USB, "running in background" and similar
        // notices from the system and its services, whatever category they use.
        if (!isLaunchableApp(context, sbn.packageName)) return null
        if (prefs.messagesOnly && !isPersonal(n)) return null

        val label = appLabel(context, sbn.packageName)
        prefs.rememberApp(sbn.packageName, label)
        if (sbn.packageName in prefs.deniedApps) return null

        var outTitle = title
        var outText = text
        if (sbn.packageName in prefs.privateApps) outText = "Content hidden"
        if (prefs.redactCodes) {
            // Each field is checked with the other as context: "Verification code" / "123456".
            outTitle = OtpRedactor.redact(title, outText, contextBefore = false)
            outText = OtpRedactor.redact(outText, title, contextBefore = true)
        }
        return Content(label, outTitle, outText)
    }

    /** Categories that are someone writing to you, or something you asked to be reminded of. */
    private val PERSONAL_CATEGORIES = setOf(
        Notification.CATEGORY_MESSAGE,
        Notification.CATEGORY_EMAIL,
        Notification.CATEGORY_EVENT,
        Notification.CATEGORY_REMINDER,
        "missed_call", // Notification.CATEGORY_MISSED_CALL, API 30
    )

    /**
     * A message, email or reminder, judged by what the app told Android: its category, or a
     * chat-style (MessagingStyle) layout for chat apps that leave the category unset. No list
     * of apps is needed, and promotions, news, shopping and game notices are left on the phone.
     */
    private fun isPersonal(n: Notification): Boolean {
        if (n.category in PERSONAL_CATEGORIES) return true
        val template = n.extras.getString(Notification.EXTRA_TEMPLATE).orEmpty()
        if (template.endsWith("\$MessagingStyle")) return true
        return n.extras.containsKey(Notification.EXTRA_MESSAGES)
    }

    /** Package -> (has a launcher icon, time looked up). */
    private val launchable = HashMap<String, Pair<Boolean, Long>>()

    /** System packages whose notifications are never app notifications. */
    private val SYSTEM_PACKAGES = setOf("android", "com.android.systemui")

    /**
     * True for an app with a launcher icon (the manifest's `<queries>` makes them visible).
     * Cached like [appLabel]; installs and removals are picked up within [LABEL_TTL_MS].
     */
    fun isLaunchableApp(context: Context, packageName: String): Boolean {
        if (packageName in SYSTEM_PACKAGES) return false
        val now = SystemClock.elapsedRealtime()
        launchable[packageName]?.let { (value, at) -> if (now - at < LABEL_TTL_MS) return value }
        val value = try {
            context.packageManager.getLaunchIntentForPackage(packageName) != null
        } catch (_: Exception) {
            true // unknown: do not hide it
        }
        if (launchable.size >= MAX_LABELS) launchable.clear()
        launchable[packageName] = value to now
        return value
    }

    /** The app's display name, cached: this runs for every notification and media callback. */
    fun appLabel(context: Context, packageName: String): String {
        val now = SystemClock.elapsedRealtime()
        labels[packageName]?.let { (label, at) -> if (now - at < LABEL_TTL_MS) return label }
        val label = try {
            val pm = context.packageManager
            pm.getApplicationLabel(pm.getApplicationInfo(packageName, 0)).toString()
        } catch (_: Exception) {
            packageName.substringAfterLast('.')
        }
        if (labels.size >= MAX_LABELS) labels.clear()
        labels[packageName] = label to now
        return label
    }

    private fun allocateId(): Int {
        val used = forwarded.values.map { it.id }.toSet()
        repeat(65_535) {
            val candidate = nextId
            nextId = if (nextId >= 65_535) 1 else nextId + 1
            if (candidate !in used) return candidate
        }
        return 1
    }
}
