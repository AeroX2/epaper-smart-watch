package com.james.epaperwatch.bridge

import android.accessibilityservice.AccessibilityService
import android.accessibilityservice.GestureDescription
import android.content.ComponentName
import android.content.Context
import android.graphics.Path
import android.graphics.Rect
import android.provider.Settings
import android.util.Log
import android.view.accessibility.AccessibilityEvent
import android.view.accessibility.AccessibilityNodeInfo

/**
 * Optional accessibility service for the watch's Camera app in Pixel Camera mode (see
 * [CameraRemote]): it presses Pixel Camera's shutter button when the watch asks.
 *
 * Android has no API for another app to press a camera app's shutter, so this finds the button
 * on screen and clicks it. Its config limits it to Pixel Camera's windows; it reads them only
 * to find the button, on request, and keeps nothing.
 */
class PixelCameraShutter : AccessibilityService() {
    override fun onServiceConnected() {
        instance = this
    }

    override fun onAccessibilityEvent(event: AccessibilityEvent?) {
        if (event?.eventType == AccessibilityEvent.TYPE_WINDOW_STATE_CHANGED &&
            event.packageName == PIXEL_CAMERA
        ) {
            CameraRemote.onPixelCameraShown()
        }
    }

    override fun onInterrupt() = Unit

    override fun onUnbind(intent: android.content.Intent?): Boolean {
        if (instance === this) instance = null
        return super.onUnbind(intent)
    }

    override fun onDestroy() {
        if (instance === this) instance = null
        super.onDestroy()
    }

    /** True while Pixel Camera is the window in front. */
    fun cameraInFront(): Boolean =
        rootInActiveWindow?.packageName?.toString() == PIXEL_CAMERA

    /**
     * Press the shutter. Clicks the button if it can be found, else taps its last known
     * position. Returns false if there is nothing to press.
     */
    fun pressShutter(): Boolean {
        val root = rootInActiveWindow
        if (root == null || root.packageName?.toString() != PIXEL_CAMERA) return false
        val button = findShutter(root)
        if (button != null) {
            val bounds = Rect().also { button.getBoundsInScreen(it) }
            if (!bounds.isEmpty) lastBounds = bounds
            if (button.performAction(AccessibilityNodeInfo.ACTION_CLICK)) return true
        }
        val target = lastBounds ?: return false
        return tap(target.exactCenterX(), target.exactCenterY())
    }

    /** Leave Pixel Camera (the watch closed its Camera app). */
    fun leaveCamera() {
        if (cameraInFront()) performGlobalAction(GLOBAL_ACTION_BACK)
    }

    private fun findShutter(root: AccessibilityNodeInfo): AccessibilityNodeInfo? {
        for (id in SHUTTER_IDS) {
            root.findAccessibilityNodeInfosByViewId(id)?.firstOrNull { it.isVisibleToUser }
                ?.let { return it }
        }
        for (label in SHUTTER_LABELS) {
            root.findAccessibilityNodeInfosByText(label)?.firstOrNull {
                it.isVisibleToUser && it.isClickable &&
                    it.contentDescription?.toString()?.contains(label, ignoreCase = true) == true
            }?.let { return it }
        }
        return null
    }

    private fun tap(x: Float, y: Float): Boolean {
        val path = Path().apply { moveTo(x, y) }
        val gesture = GestureDescription.Builder()
            .addStroke(GestureDescription.StrokeDescription(path, 0, 60))
            .build()
        return dispatchGesture(gesture, null, null)
    }

    companion object {
        const val PIXEL_CAMERA = "com.google.android.GoogleCamera"
        // Pixel Camera 10 (Pixel 10 Pro Fold, Nov 2026): a Compose button tagged
        // "ComposeShutter", described "Take photo". The others are older versions.
        private val SHUTTER_IDS = listOf(
            "ComposeShutter",
            "$PIXEL_CAMERA:id/shutter_button",
            "$PIXEL_CAMERA:id/shutter",
        )
        private val SHUTTER_LABELS = listOf("Shutter", "Take photo", "Capture")

        @Volatile
        var instance: PixelCameraShutter? = null
            private set

        /** Where the shutter was last found; Pixel Camera keeps it in one place. */
        private var lastBounds: Rect? = null

        fun isEnabled(context: Context): Boolean {
            val wanted = ComponentName(context, PixelCameraShutter::class.java)
            val enabled = Settings.Secure.getString(
                context.contentResolver, Settings.Secure.ENABLED_ACCESSIBILITY_SERVICES,
            ) ?: return false
            return enabled.split(':').any {
                ComponentName.unflattenFromString(it) == wanted
            }.also { if (!it) Log.i("PixelCameraShutter", "Service not enabled") }
        }
    }
}
