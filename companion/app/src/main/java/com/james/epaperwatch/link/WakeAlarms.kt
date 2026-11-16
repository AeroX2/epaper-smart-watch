package com.james.epaperwatch.link

import android.app.AlarmManager
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Build
import android.os.SystemClock
import android.util.Log

/**
 * One-shot timers that keep working in Doze, for the link service's slow periodic work
 * (weather refresh, calendar re-evaluation, time resync). `Handler.postDelayed` counts
 * uptime, which stands still in deep sleep.
 *
 * Uses inexact `AlarmManager.setAndAllowWhileIdle` (no exact-alarm permission): the system
 * may deliver late, and in Doze at most about once every nine minutes per app, which is fine
 * for these jobs. Alarms are delivered as a package-private broadcast to a receiver that only
 * exists while the service runs; [shutdown] cancels everything.
 */
class WakeAlarms(
    private val context: Context,
    private val tags: List<String>,
    private val onAlarm: (String) -> Unit,
) {
    private val alarmManager = context.getSystemService(AlarmManager::class.java)
    private var registered = false

    private val receiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            val tag = intent.getStringExtra(EXTRA_TAG) ?: return
            if (tag in tags) onAlarm(tag)
        }
    }

    fun register() {
        if (registered) return
        val filter = IntentFilter(ACTION)
        if (Build.VERSION.SDK_INT >= 33) {
            context.registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED)
        } else {
            @Suppress("UnspecifiedRegisterReceiverFlag")
            context.registerReceiver(receiver, filter)
        }
        registered = true
    }

    /** (Re)arm [tag] to fire once, about [delayMs] from now. Replaces a pending alarm. */
    fun schedule(tag: String, delayMs: Long) {
        val am = alarmManager ?: return
        try {
            am.setAndAllowWhileIdle(
                AlarmManager.ELAPSED_REALTIME_WAKEUP,
                SystemClock.elapsedRealtime() + delayMs.coerceAtLeast(MIN_DELAY_MS),
                pendingIntent(tag),
            )
        } catch (e: RuntimeException) {
            Log.w(TAG, "Could not schedule $tag", e)
        }
    }

    fun cancel(tag: String) {
        try {
            alarmManager?.cancel(pendingIntent(tag))
        } catch (e: RuntimeException) {
            Log.w(TAG, "Could not cancel $tag", e)
        }
    }

    /** Cancel every alarm and stop receiving. */
    fun shutdown() {
        tags.forEach(::cancel)
        if (registered) {
            context.unregisterReceiver(receiver)
            registered = false
        }
    }

    private fun pendingIntent(tag: String): PendingIntent = PendingIntent.getBroadcast(
        context,
        tags.indexOf(tag), // extras do not distinguish PendingIntents; the request code does
        Intent(ACTION).setPackage(context.packageName).putExtra(EXTRA_TAG, tag),
        PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
    )

    private companion object {
        const val TAG = "WakeAlarms"
        const val ACTION = "com.james.epaperwatch.WAKE_ALARM"
        const val EXTRA_TAG = "tag"
        const val MIN_DELAY_MS = 1_000L
    }
}
