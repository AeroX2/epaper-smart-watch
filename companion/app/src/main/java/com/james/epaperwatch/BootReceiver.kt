package com.james.epaperwatch

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent

/** Restarts the link service after boot or an app update, if a watch is associated. */
class BootReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        when (intent.action) {
            Intent.ACTION_BOOT_COMPLETED,
            Intent.ACTION_MY_PACKAGE_REPLACED -> WatchLinkService.start(context)
        }
    }
}
