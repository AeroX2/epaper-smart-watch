package com.james.epaperwatch.bridge

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.media.AudioAttributes
import android.media.AudioManager
import android.media.Ringtone
import android.media.RingtoneManager
import android.os.Build
import android.os.Handler
import android.os.VibrationAttributes
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import android.util.Log
import com.james.epaperwatch.data.Prefs
import kotlin.math.ceil

/**
 * FIND_PHONE: rings the default alarm sound on the alarm stream (so it bypasses ringer mode),
 * vibrates, and shows a notification with a Stop action. Stops on FIND_PHONE|0, the Stop
 * action, or after [MAX_RING_MS].
 *
 * A phone whose alarm volume is low or zero would ring silently, so while ringing the alarm
 * stream is raised to at least [MIN_VOLUME_FRACTION] of its maximum and put back on [stop]
 * (every stop path: watch, Stop action, auto-stop, service shutdown). The previous volume is
 * also kept in prefs, so a process death while ringing is repaired by [restoreVolumeIfLeftRaised].
 */
class FindPhoneRinger(
    private val context: Context,
    private val handler: Handler,
    private val stopIntent: Intent,
) {
    private val prefs = Prefs(context)
    private var ringtone: Ringtone? = null
    private var ringing = false
    private val autoStop = Runnable { stop() }

    val isRinging: Boolean get() = ringing

    fun start() {
        if (ringing) return
        ringing = true
        raiseVolume()
        val uri = RingtoneManager.getDefaultUri(RingtoneManager.TYPE_ALARM)
            ?: RingtoneManager.getDefaultUri(RingtoneManager.TYPE_RINGTONE)
        val tone = RingtoneManager.getRingtone(context, uri)
        if (tone != null) {
            tone.audioAttributes = AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_ALARM)
                .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                .build()
            if (Build.VERSION.SDK_INT >= 28) tone.isLooping = true
            try {
                tone.play()
            } catch (e: Exception) {
                Log.w(TAG, "Ringtone failed", e)
            }
        }
        ringtone = tone
        vibrate()
        showNotification()
        handler.postDelayed(autoStop, MAX_RING_MS)
    }

    fun stop() {
        handler.removeCallbacks(autoStop)
        if (!ringing) return
        ringing = false
        try {
            ringtone?.stop()
        } catch (_: Exception) {
        }
        ringtone = null
        restoreVolume()
        vibrator()?.cancel()
        context.getSystemService(NotificationManager::class.java)?.cancel(NOTIFICATION_ID)
    }

    /** Call at service start: undo a raise that a killed process could not undo. */
    fun restoreVolumeIfLeftRaised() {
        if (!ringing) restoreVolume()
    }

    private fun raiseVolume() {
        val audio = context.getSystemService(AudioManager::class.java) ?: return
        try {
            val max = audio.getStreamMaxVolume(AudioManager.STREAM_ALARM)
            val current = audio.getStreamVolume(AudioManager.STREAM_ALARM)
            val wanted = ceil(max * MIN_VOLUME_FRACTION).toInt().coerceIn(1, max)
            if (current >= wanted) return
            audio.setStreamVolume(AudioManager.STREAM_ALARM, wanted, 0)
            prefs.findPhoneSavedVolume = current
            prefs.findPhoneRaisedVolume = wanted
        } catch (e: RuntimeException) {
            // e.g. SecurityException under some Do Not Disturb policies: ring at what we have.
            Log.w(TAG, "Could not raise the alarm volume", e)
        }
    }

    private fun restoreVolume() {
        val saved = prefs.findPhoneSavedVolume
        if (saved < 0) return
        val raised = prefs.findPhoneRaisedVolume
        prefs.findPhoneSavedVolume = -1
        val audio = context.getSystemService(AudioManager::class.java) ?: return
        try {
            // If the user changed the volume while it rang, their choice stands.
            if (audio.getStreamVolume(AudioManager.STREAM_ALARM) == raised) {
                audio.setStreamVolume(AudioManager.STREAM_ALARM, saved, 0)
            }
        } catch (e: RuntimeException) {
            Log.w(TAG, "Could not restore the alarm volume", e)
        }
    }

    private fun vibrator(): Vibrator? = if (Build.VERSION.SDK_INT >= 31) {
        context.getSystemService(VibratorManager::class.java)?.defaultVibrator
    } else {
        @Suppress("DEPRECATION")
        context.getSystemService(Context.VIBRATOR_SERVICE) as? Vibrator
    }

    private fun vibrate() {
        val vibrator = vibrator() ?: return
        val effect = VibrationEffect.createWaveform(longArrayOf(0, 700, 500), 0)
        if (Build.VERSION.SDK_INT >= 33) {
            vibrator.vibrate(effect, VibrationAttributes.createForUsage(VibrationAttributes.USAGE_ALARM))
        } else {
            @Suppress("DEPRECATION")
            vibrator.vibrate(
                effect,
                AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_ALARM).build(),
            )
        }
    }

    private fun showNotification() {
        val nm = context.getSystemService(NotificationManager::class.java) ?: return
        nm.createNotificationChannel(
            NotificationChannel(CHANNEL_ID, "Find my phone", NotificationManager.IMPORTANCE_HIGH)
                .apply { setSound(null, null) },
        )
        val stop = PendingIntent.getService(
            context, 2, stopIntent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        val notification = Notification.Builder(context, CHANNEL_ID)
            .setSmallIcon(android.R.drawable.ic_lock_idle_alarm)
            .setContentTitle("Your watch is looking for this phone")
            .setContentText("Tap Stop to silence")
            .setCategory(Notification.CATEGORY_ALARM)
            .setOngoing(true)
            .setContentIntent(stop)
            .setDeleteIntent(stop)
            .addAction(Notification.Action.Builder(null, "Stop", stop).build())
            .build()
        try {
            nm.notify(NOTIFICATION_ID, notification)
        } catch (e: SecurityException) {
            Log.w(TAG, "Cannot post find-phone notification", e)
        }
    }

    companion object {
        private const val TAG = "FindPhone"
        private const val CHANNEL_ID = "find-phone"
        private const val NOTIFICATION_ID = 72
        private const val MAX_RING_MS = 2L * 60 * 1000
        private const val MIN_VOLUME_FRACTION = 0.7
    }
}
