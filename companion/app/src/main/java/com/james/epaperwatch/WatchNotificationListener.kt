package com.james.epaperwatch

import android.app.Notification
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification

class WatchNotificationListener : NotificationListenerService() {
    override fun onListenerConnected() {
        WatchLinkService.start(this)
    }

    override fun onNotificationPosted(sbn: StatusBarNotification) {
        if (sbn.packageName == packageName ||
            sbn.notification.flags and Notification.FLAG_GROUP_SUMMARY != 0
        ) return

        val extras = sbn.notification.extras
        val title = extras.getCharSequence(Notification.EXTRA_TITLE) ?: return
        val text = extras.getCharSequence(Notification.EXTRA_BIG_TEXT)
            ?: extras.getCharSequence(Notification.EXTRA_TEXT)
            ?: return
        val appName = try {
            val info = packageManager.getApplicationInfo(sbn.packageName, 0)
            packageManager.getApplicationLabel(info)
        } catch (_: Exception) {
            sbn.packageName.substringAfterLast('.')
        }
        WatchLinkService.send(this, WatchProtocol.notification(appName, title, text))
    }
}
