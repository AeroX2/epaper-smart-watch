package com.james.epaperwatch.bridge

import android.Manifest
import android.annotation.SuppressLint
import android.app.Notification
import android.app.PendingIntent
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.service.notification.StatusBarNotification
import android.telecom.TelecomManager
import android.util.Log
import com.james.epaperwatch.WatchLinkService
import com.james.epaperwatch.data.Prefs
import com.james.epaperwatch.protocol.Out
import com.james.epaperwatch.protocol.OutMessage

/**
 * Incoming-call relay driven by CATEGORY_CALL notifications (works for the dialer and for VoIP
 * apps, and needs no phone-state permission).
 *
 * Ringing -> `CALL|RING|caller`; answered (notification turns into an ongoing call) or removed
 * -> `CALL|END`. `CALL_REJECT` uses TelecomManager.endCall() when ANSWER_PHONE_CALLS is granted
 * (API 28+), otherwise it fires the notification's Decline/Reject action if it has one.
 */
object CallBridge {
    private const val TAG = "CallBridge"
    private const val CALL_TYPE_INCOMING = 1 // Notification.CallStyle.CALL_TYPE_INCOMING
    private const val CALL_TYPE_ONGOING = 2
    private const val CALL_TYPE_SCREENING = 3
    private const val EXTRA_CALL_TYPE = "android.callType" // Notification.EXTRA_CALL_TYPE

    private val DECLINE = Regex("\\b(decline|reject|hang up|end call)\\b", RegexOption.IGNORE_CASE)

    private var ringing: StatusBarNotification? = null
    private var caller: String = ""

    /** Returns true if the notification is a call notification (and so must not be forwarded). */
    fun onPosted(context: Context, sbn: StatusBarNotification): Boolean {
        if (sbn.notification.category != Notification.CATEGORY_CALL) return false
        if (sbn.packageName == context.packageName) return true
        val prefs = Prefs(context)
        if (!prefs.forwardCalls || sbn.packageName in prefs.deniedApps) return true

        if (isRinging(sbn)) {
            val name = callerName(sbn)
            val changed = ringing?.key != sbn.key || name != caller
            ringing = sbn
            caller = name
            if (changed) WatchLinkService.send(Out.callRing(name))
        } else if (ringing?.key == sbn.key) {
            end()
        }
        return true
    }

    fun onRemoved(sbn: StatusBarNotification) {
        if (ringing?.key == sbn.key) end()
    }

    /** Listener rebind: end a ringing call whose notification vanished while unbound. */
    fun retain(activeKeys: Set<String>) {
        val key = ringing?.key ?: return
        if (key !in activeKeys) end()
    }

    /** Re-announce a call that is still ringing when a session (re)starts. */
    fun sessionMessage(): OutMessage? = if (ringing != null) Out.callRing(caller) else null

    @SuppressLint("MissingPermission")
    fun reject(context: Context) {
        // Only while a call is ringing: endCall() would otherwise hang up an answered call
        // (CALL_REJECT can cross with our CALL|END on the air).
        val sbn = ringing ?: return
        var ended = false
        if (Build.VERSION.SDK_INT >= 28 && Prefs(context).rejectCalls &&
            context.checkSelfPermission(Manifest.permission.ANSWER_PHONE_CALLS) ==
            PackageManager.PERMISSION_GRANTED
        ) {
            ended = try {
                @Suppress("DEPRECATION")
                context.getSystemService(TelecomManager::class.java)?.endCall() == true
            } catch (e: Exception) {
                Log.w(TAG, "endCall failed", e)
                false
            }
        }
        if (!ended) ended = fireDeclineAction(sbn)
        if (!ended) Log.i(TAG, "Could not reject call (no permission and no decline action)")
    }

    private fun fireDeclineAction(sbn: StatusBarNotification): Boolean {
        val action = sbn.notification.actions?.firstOrNull { a ->
            val title = a.title?.toString() ?: return@firstOrNull false
            DECLINE.containsMatchIn(title)
        } ?: return false
        return try {
            action.actionIntent.send()
            true
        } catch (e: PendingIntent.CanceledException) {
            false
        }
    }

    private fun end() {
        ringing = null
        caller = ""
        WatchLinkService.send(Out.callEnd())
    }

    private fun isRinging(sbn: StatusBarNotification): Boolean {
        val n = sbn.notification
        if (Build.VERSION.SDK_INT >= 31) {
            when (n.extras.getInt(EXTRA_CALL_TYPE, 0)) {
                CALL_TYPE_INCOMING, CALL_TYPE_SCREENING -> return true
                CALL_TYPE_ONGOING -> return false
            }
        }
        // Pre-CallStyle heuristic: incoming-call notifications carry a full-screen intent or
        // ring insistently; in-call notifications do neither.
        return n.fullScreenIntent != null || n.flags and Notification.FLAG_INSISTENT != 0
    }

    private fun callerName(sbn: StatusBarNotification): String {
        val extras = sbn.notification.extras
        return (extras.getCharSequence(Notification.EXTRA_TITLE)
            ?: extras.getCharSequence(Notification.EXTRA_TEXT))?.toString()?.takeIf { it.isNotBlank() }
            ?: "Unknown caller"
    }
}
