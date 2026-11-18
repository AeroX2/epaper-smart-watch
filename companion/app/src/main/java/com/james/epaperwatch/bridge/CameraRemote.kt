package com.james.epaperwatch.bridge

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.Log
import com.james.epaperwatch.protocol.Out
import com.james.epaperwatch.protocol.OutMessage
import com.james.epaperwatch.ui.CameraActivity
import java.lang.ref.WeakReference

/**
 * The watch's Camera app: CAMERA|OPEN opens [CameraActivity], CAMERA|SHUTTER|delay takes a
 * photo, CAMERA|CLOSE closes it. The activity reports its state back through [report].
 *
 * Starting an activity from the background is allowed here because the watch is associated
 * through CompanionDeviceManager. If the phone still does not show it (some vendors block it),
 * a notification asks the user to tap it and the watch says so.
 */
object CameraRemote {
    private const val TAG = "CameraRemote"
    private const val CHANNEL_ID = "camera"
    private const val NOTIFICATION_ID = 4107
    private const val OPEN_TIMEOUT_MS = 3_000L

    private val handler = Handler(Looper.getMainLooper())
    private var activity: WeakReference<CameraActivity>? = null
    private var appContext: Context? = null

    /** Set by WatchLinkService while it runs; sends a message to the watch. */
    var sender: ((OutMessage) -> Unit)? = null

    private val openTimeout = Runnable {
        val context = appContext ?: return@Runnable
        if (current() == null) {
            Log.w(TAG, "Camera screen did not open from the background")
            showTapNotification(context)
            report("ERROR", detail = "TAP THE PHONE")
        }
    }

    private fun current(): CameraActivity? = activity?.get()?.takeUnless { it.isFinishing }

    fun open(context: Context) {
        appContext = context.applicationContext
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
        val open = current()
        if (open == null) report("CLOSED") else open.shutter(delaySeconds.coerceIn(0, 30))
    }

    fun close() {
        handler.removeCallbacks(openTimeout)
        current()?.closeFromWatch()
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
        val pending = PendingIntent.getActivity(
            context, 0, intent(context),
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
