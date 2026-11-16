package com.james.epaperwatch.bridge

import android.app.Notification
import android.content.Context
import android.service.notification.StatusBarNotification
import android.util.Log
import com.james.epaperwatch.WatchLinkService
import com.james.epaperwatch.WatchNotificationListener
import com.james.epaperwatch.data.Prefs
import com.james.epaperwatch.protocol.Out
import com.james.epaperwatch.protocol.OutMessage

/**
 * Mirrors the phone's notification shade onto the watch.
 *
 * - Filters out our own app, group summaries, ongoing / foreground-service notifications and
 *   the progress/transport/service/system/status categories, plus apps the user has denied.
 * - Each StatusBarNotification key gets a stable small id (1..65535) for as long as it is shown.
 * - Re-posts with identical (sanitised) content are not re-sent.
 * - `NOTIFY.alert` is 1 only the first time a key is forwarded from a live post; content
 *   updates of a forwarded key, session-start replays and filter re-evaluations are silent.
 * - Removal sends NOTIFY_CLEAR; NOTIFY_DISMISS from the watch cancels the Android notification.
 *
 * Called on the notification listener's main thread.
 */
object NotificationRelay {
    /** [message] is the silent (alert = 0) form, used for replays and for comparing content. */
    private class Entry(val key: String, val id: Int, val message: OutMessage, val postTime: Long)

    private val forwarded = LinkedHashMap<String, Entry>()
    private var nextId = 1

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
        val id = existing?.id ?: allocateId()
        val silent = message(id, message, alert = false)
        forwarded.remove(sbn.key)
        forwarded[sbn.key] = Entry(sbn.key, id, silent, sbn.postTime)
        if (!send) return
        // Alert only for a genuinely new arrival: the first time this key is forwarded.
        val alert = live && existing == null
        WatchLinkService.send(if (alert) message(id, message, alert = true) else silent)
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
        forwarded.values.sortedBy { it.postTime }.takeLast(max).map { it.message }

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

    fun appLabel(context: Context, packageName: String): String = try {
        val pm = context.packageManager
        pm.getApplicationLabel(pm.getApplicationInfo(packageName, 0)).toString()
    } catch (_: Exception) {
        packageName.substringAfterLast('.')
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
