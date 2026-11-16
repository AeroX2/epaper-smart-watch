package com.james.epaperwatch

import android.companion.AssociationInfo
import android.companion.CompanionDeviceService
import android.os.Build
import android.annotation.TargetApi

/**
 * Bound by the system (API 31+) when the associated watch comes into BLE range, because we call
 * CompanionDeviceManager.startObservingDevicePresence(). Restarts the link service if the
 * process had been killed; the link's pending auto-connect then completes.
 */
@TargetApi(Build.VERSION_CODES.S)
class WatchCompanionService : CompanionDeviceService() {
    @Deprecated("Replaced by onDeviceAppeared(AssociationInfo) on API 33+")
    override fun onDeviceAppeared(address: String) {
        WatchLinkService.start(this)
    }

    @TargetApi(Build.VERSION_CODES.TIRAMISU)
    override fun onDeviceAppeared(associationInfo: AssociationInfo) {
        WatchLinkService.start(this)
    }

    @Deprecated("Replaced by onDeviceDisappeared(AssociationInfo) on API 33+")
    override fun onDeviceDisappeared(address: String) {
        // Keep the service and its auto-connect; nothing to do.
    }
}
