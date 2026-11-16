package com.james.epaperwatch

import android.annotation.SuppressLint
import android.app.NotificationManager
import android.content.ComponentName
import android.content.Context
import android.os.Build
import android.provider.Settings
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification
import android.util.Log
import com.james.epaperwatch.bridge.NotificationRelay

/** Feeds posted/removed notifications into [NotificationRelay] and enables media sessions access. */
class WatchNotificationListener : NotificationListenerService() {
    override fun onListenerConnected() {
        instance = this
        NotificationRelay.seed(this, activeSafely())
        WatchLinkService.instance?.onNotificationAccessGranted()
        WatchLinkService.start(this)
    }

    override fun onListenerDisconnected() {
        instance = null
        requestRebind(ComponentName(this, WatchNotificationListener::class.java))
    }

    override fun onNotificationPosted(sbn: StatusBarNotification) {
        NotificationRelay.onPosted(this, sbn)
    }

    override fun onNotificationRemoved(sbn: StatusBarNotification) {
        NotificationRelay.onRemoved(sbn)
    }

    private fun activeSafely(): Array<StatusBarNotification> = try {
        activeNotifications ?: emptyArray()
    } catch (e: Exception) {
        emptyArray()
    }

    companion object {
        @SuppressLint("StaticFieldLeak")
        @Volatile
        private var instance: WatchNotificationListener? = null

        fun cancel(key: String) {
            try {
                instance?.cancelNotification(key)
            } catch (e: Exception) {
                Log.w("NotificationListener", "cancel failed", e)
            }
        }

        /** Re-apply filters to everything currently shown (after a settings change). */
        fun reevaluate() {
            val listener = instance ?: return
            NotificationRelay.reevaluate(listener, listener.activeSafely())
        }

        fun isEnabled(context: Context): Boolean {
            val component = ComponentName(context, WatchNotificationListener::class.java)
            if (Build.VERSION.SDK_INT >= 27) {
                return context.getSystemService(NotificationManager::class.java)
                    ?.isNotificationListenerAccessGranted(component) == true
            }
            val enabled = Settings.Secure.getString(
                context.contentResolver, "enabled_notification_listeners",
            ) ?: return false
            return enabled.split(':').any { ComponentName.unflattenFromString(it) == component }
        }
    }
}
