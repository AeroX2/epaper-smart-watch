package com.james.epaperwatch.bridge

import android.app.KeyguardManager
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.media.AudioManager
import android.media.ToneGenerator
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.provider.MediaStore
import android.util.Log
import com.james.epaperwatch.data.Prefs
import com.james.epaperwatch.protocol.Out
import com.james.epaperwatch.protocol.OutMessage
import com.james.epaperwatch.ui.CameraActivity
import java.lang.ref.WeakReference

/**
 * The watch's Camera app: CAMERA|OPEN, CAMERA|SHUTTER|delay and CAMERA|CLOSE.
 *
 * Two ways to take the photo, chosen in the app's settings:
 * - this app's own camera screen ([CameraActivity], CameraX), the default;
 * - Pixel Camera, whose shutter is pressed by the optional [PixelCameraShutter] accessibility
 *   service (Android has no API for pressing another app's shutter). Used only while that
 *   service is enabled; otherwise the own screen is used.
 *
 * Starting an activity from the background is allowed because the watch is associated through
 * CompanionDeviceManager. If the phone still does not show it (some vendors block it), a
 * notification asks the user to tap it and the watch says so.
 */
object CameraRemote {
    private const val TAG = "CameraRemote"
    private const val CHANNEL_ID = "camera"
    private const val NOTIFICATION_ID = 4107
    private const val OPEN_TIMEOUT_MS = 3_000L

    private val handler = Handler(Looper.getMainLooper())
    private var activity: WeakReference<CameraActivity>? = null
    private var appContext: Context? = null

    /** Pixel Camera mode is in use for the current session of the watch's Camera app. */
    private var pixelMode = false
    private var pixelShown = false
    private var pixelPhotos = 0
    private var pixelCountdown = 0
    private var beeper: ToneGenerator? = null

    /** Set by WatchLinkService while it runs; sends a message to the watch. */
    var sender: ((OutMessage) -> Unit)? = null

    private val openTimeout = Runnable {
        val context = appContext ?: return@Runnable
        val shown = if (pixelMode) pixelShown else current() != null
        if (!shown) {
            Log.w(TAG, "Camera did not open from the background")
            showTapNotification(context)
            report("ERROR", detail = "TAP THE PHONE")
        }
    }

    private val pixelTick = object : Runnable {
        override fun run() {
            pixelCountdown--
            if (pixelCountdown <= 0) {
                pressPixelShutter()
            } else {
                beep(short = true)
                handler.postDelayed(this, 1_000L)
            }
        }
    }

    private fun current(): CameraActivity? = activity?.get()?.takeUnless { it.isFinishing }

    fun open(context: Context) {
        appContext = context.applicationContext
        pixelMode = Prefs(context).cameraPixel && PixelCameraShutter.isEnabled(context)
        if (pixelMode) {
            openPixelCamera(context)
            return
        }
        val open = current()
        if (open != null) {
            open.reportState()
            return
        }
        try {
            context.startActivity(intent(context))
        } catch (e: RuntimeException) {
            Log.w(TAG, "Could not start the camera screen", e)
        }
        handler.removeCallbacks(openTimeout)
        handler.postDelayed(openTimeout, OPEN_TIMEOUT_MS)
    }

    fun shutter(delaySeconds: Int) {
        val delay = delaySeconds.coerceIn(0, 30)
        if (pixelMode) {
            pixelShutter(delay)
            return
        }
        val open = current()
        if (open == null) report("CLOSED") else open.shutter(delay)
    }

    fun close() {
        handler.removeCallbacks(openTimeout)
        if (pixelMode) {
            handler.removeCallbacks(pixelTick)
            pixelCountdown = 0
            PixelCameraShutter.instance?.leaveCamera()
            pixelMode = false
            return
        }
        current()?.closeFromWatch()
    }

    // ---- Pixel Camera mode

    private fun openPixelCamera(context: Context) {
        pixelShown = false
        pixelPhotos = 0
        pixelCountdown = 0
        if (PixelCameraShutter.instance?.cameraInFront() == true) {
            onPixelCameraShown()
            return
        }
        // The secure variant opens over the lock screen without unlocking the phone.
        val locked = context.getSystemService(KeyguardManager::class.java)?.isKeyguardLocked == true
        val action = if (locked) {
            MediaStore.INTENT_ACTION_STILL_IMAGE_CAMERA_SECURE
        } else {
            MediaStore.INTENT_ACTION_STILL_IMAGE_CAMERA
        }
        val intent = Intent(action)
            .setPackage(PixelCameraShutter.PIXEL_CAMERA)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        try {
            context.startActivity(intent)
            report("OPENING")
        } catch (e: RuntimeException) {
            Log.w(TAG, "Could not open Pixel Camera", e)
            pixelMode = false
            report("ERROR", detail = "NO PIXEL CAMERA")
            return
        }
        handler.removeCallbacks(openTimeout)
        handler.postDelayed(openTimeout, OPEN_TIMEOUT_MS)
    }

    /** Called by [PixelCameraShutter] when a Pixel Camera window comes to the front. */
    fun onPixelCameraShown() {
        if (!pixelMode || pixelShown) return
        pixelShown = true
        handler.removeCallbacks(openTimeout)
        appContext?.getSystemService(NotificationManager::class.java)?.cancel(NOTIFICATION_ID)
        report("READY")
    }

    private fun pixelShutter(delay: Int) {
        val service = PixelCameraShutter.instance
        if (service == null || !service.cameraInFront()) {
            report("CLOSED")
            return
        }
        if (pixelCountdown > 0) return
        if (delay == 0) {
            pressPixelShutter()
            return
        }
        pixelCountdown = delay
        beep(short = true)
        report("COUNTDOWN", delay)
        handler.postDelayed(pixelTick, 1_000L)
    }

    private fun pressPixelShutter() {
        pixelCountdown = 0
        val pressed = PixelCameraShutter.instance?.pressShutter() == true
        if (pressed) {
            pixelPhotos++
            report("SAVED", pixelPhotos)
        } else {
            report("ERROR", detail = "NO SHUTTER FOUND")
            // Let the next press try again.
            handler.postDelayed({ if (pixelMode) report("READY") }, 2_000L)
        }
    }

    private fun beep(short: Boolean) {
        try {
            val tones = beeper ?: ToneGenerator(AudioManager.STREAM_SYSTEM, 80).also { beeper = it }
            tones.startTone(ToneGenerator.TONE_PROP_BEEP, if (short) 120 else 200)
        } catch (e: RuntimeException) {
            Log.w(TAG, "Beep failed", e)
        }
    }

    // ---- called by CameraActivity

    fun attach(camera: CameraActivity) {
        activity = WeakReference(camera)
        handler.removeCallbacks(openTimeout)
        camera.getSystemService(NotificationManager::class.java)?.cancel(NOTIFICATION_ID)
    }

    fun detach(camera: CameraActivity) {
        if (activity?.get() === camera) activity = null
    }

    fun report(state: String, number: Int = 0, detail: String = "") {
        sender?.invoke(Out.camera(state, number, detail))
    }

    private fun intent(context: Context) =
        Intent(context, CameraActivity::class.java)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_SINGLE_TOP)

    private fun showTapNotification(context: Context) {
        val manager = context.getSystemService(NotificationManager::class.java) ?: return
        if (Build.VERSION.SDK_INT >= 26) {
            manager.createNotificationChannel(
                NotificationChannel(CHANNEL_ID, "Camera remote", NotificationManager.IMPORTANCE_HIGH),
            )
        }
        val target = if (pixelMode) {
            Intent(MediaStore.INTENT_ACTION_STILL_IMAGE_CAMERA)
                .setPackage(PixelCameraShutter.PIXEL_CAMERA)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        } else {
            intent(context)
        }
        val pending = PendingIntent.getActivity(
            context, 0, target,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE,
        )
        val builder = if (Build.VERSION.SDK_INT >= 26) {
            Notification.Builder(context, CHANNEL_ID)
        } else {
            @Suppress("DEPRECATION")
            Notification.Builder(context)
        }
        val notification = builder
            .setSmallIcon(android.R.drawable.ic_menu_camera)
            .setContentTitle("Watch camera")
            .setContentText("Tap to open the camera for the watch")
            .setContentIntent(pending)
            .setAutoCancel(true)
            .setCategory(Notification.CATEGORY_REMINDER)
            .build()
        try {
            manager.notify(NOTIFICATION_ID, notification)
        } catch (e: SecurityException) {
            Log.w(TAG, "Notification permission missing", e)
        }
    }
}
