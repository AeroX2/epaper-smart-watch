package com.james.epaperwatch.bridge

import android.Manifest
import android.content.ContentUris
import android.content.Context
import android.content.pm.PackageManager
import android.database.ContentObserver
import android.os.Handler
import android.provider.CalendarContract
import android.util.Log
import com.james.epaperwatch.data.Prefs
import com.james.epaperwatch.protocol.Out
import com.james.epaperwatch.protocol.OutMessage

/**
 * EVENT source: the next visible, non-all-day, non-declined event starting within 24 h.
 * Watches the calendar provider and re-evaluates when the reported event starts. That moment
 * can be hours away, so it is armed through [scheduleWake] (a Doze-safe alarm owned by the
 * service, which then calls [eventMessage] again) rather than a Handler delay.
 */
class CalendarBridge(
    private val context: Context,
    private val handler: Handler,
    private val scheduleWake: (delayMs: Long) -> Unit,
    private val cancelWake: () -> Unit,
    private val onChange: () -> Unit,
) {
    private var observing = false
    private val notify = Runnable { onChange() }

    private val observer = object : ContentObserver(handler) {
        override fun onChange(selfChange: Boolean) {
            handler.removeCallbacks(notify)
            handler.postDelayed(notify, 2_000L)
        }
    }

    fun isEnabled(): Boolean = Prefs(context).calendarEnabled && hasPermission(context)

    /** Start or stop observing according to the current setting. */
    fun refreshRegistration() {
        if (isEnabled() && !observing) {
            try {
                context.contentResolver.registerContentObserver(
                    CalendarContract.CONTENT_URI, true, observer,
                )
                observing = true
            } catch (e: SecurityException) {
                Log.w(TAG, "Calendar observer refused", e)
            }
        } else if (!isEnabled() && observing) {
            context.contentResolver.unregisterContentObserver(observer)
            observing = false
        }
    }

    fun stop() {
        handler.removeCallbacks(notify)
        cancelWake()
        if (observing) context.contentResolver.unregisterContentObserver(observer)
        observing = false
    }

    fun eventMessage(): OutMessage {
        handler.removeCallbacks(notify)
        cancelWake()
        if (!isEnabled()) return Out.event(0, null)
        val now = System.currentTimeMillis()
        val uri = CalendarContract.Instances.CONTENT_URI.buildUpon().also {
            ContentUris.appendId(it, now)
            ContentUris.appendId(it, now + DAY_MS)
        }.build()
        val projection = arrayOf(CalendarContract.Instances.BEGIN, CalendarContract.Instances.TITLE)
        val selection = "${CalendarContract.Instances.VISIBLE} = 1 AND " +
            "${CalendarContract.Instances.ALL_DAY} = 0 AND " +
            "${CalendarContract.Instances.BEGIN} >= ? AND " +
            "${CalendarContract.Instances.SELF_ATTENDEE_STATUS} != " +
            "${CalendarContract.Attendees.ATTENDEE_STATUS_DECLINED}"
        try {
            context.contentResolver.query(
                uri, projection, selection, arrayOf(now.toString()),
                "${CalendarContract.Instances.BEGIN} ASC",
            )?.use { cursor ->
                if (cursor.moveToFirst()) {
                    val begin = cursor.getLong(0)
                    val title = cursor.getString(1)
                    // Move on to the following event once this one has started.
                    scheduleWake((begin - now + 1_000L).coerceAtLeast(1_000L))
                    return Out.event(begin / 1000L, title?.takeIf { it.isNotBlank() } ?: "Event")
                }
            }
        } catch (e: Exception) {
            Log.w(TAG, "Calendar query failed", e)
        }
        return Out.event(0, null)
    }

    companion object {
        private const val TAG = "CalendarBridge"
        private const val DAY_MS = 24L * 60 * 60 * 1000

        fun hasPermission(context: Context) =
            context.checkSelfPermission(Manifest.permission.READ_CALENDAR) ==
                PackageManager.PERMISSION_GRANTED
    }
}
