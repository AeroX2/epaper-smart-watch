package com.james.epaperwatch.pairing

import android.annotation.SuppressLint
import android.app.Activity
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.companion.AssociationInfo
import android.companion.AssociationRequest
import android.companion.BluetoothLeDeviceFilter
import android.companion.CompanionDeviceManager
import android.content.Context
import android.content.Intent
import android.content.IntentSender
import android.os.Build
import android.os.ParcelUuid
import android.os.Parcelable
import android.util.Log
import com.james.epaperwatch.WatchLinkService
import com.james.epaperwatch.data.Prefs
import com.james.epaperwatch.data.WatchState
import com.james.epaperwatch.protocol.Protocol
import java.util.Locale

/**
 * Companion Device Manager association: the system chooser scans for the watch service UUID,
 * the chosen device's MAC is persisted, and on API 31+ presence observation is started so the
 * system wakes [com.james.epaperwatch.WatchCompanionService] when the watch is in range.
 */
object CompanionPairing {
    const val REQUEST_ASSOCIATE = 7001
    private const val TAG = "CompanionPairing"

    fun isSupported(context: Context): Boolean =
        context.packageManager.hasSystemFeature("android.software.companion_device_setup")

    fun startAssociation(activity: Activity, onError: (String) -> Unit) {
        val cdm = activity.getSystemService(CompanionDeviceManager::class.java)
        if (cdm == null || !isSupported(activity)) {
            onError("This phone does not support companion device pairing")
            return
        }
        val scanFilter = ScanFilter.Builder()
            .setServiceUuid(ParcelUuid(Protocol.SERVICE_UUID))
            .build()
        val deviceFilter = BluetoothLeDeviceFilter.Builder().setScanFilter(scanFilter).build()
        val request = AssociationRequest.Builder()
            .addDeviceFilter(deviceFilter)
            .setSingleDevice(false)
            .build()

        if (Build.VERSION.SDK_INT >= 33) {
            cdm.associate(request, activity.mainExecutor, object : CompanionDeviceManager.Callback() {
                override fun onAssociationPending(intentSender: IntentSender) {
                    launchChooser(activity, intentSender, onError)
                }

                override fun onAssociationCreated(associationInfo: AssociationInfo) {
                    val mac = associationInfo.deviceMacAddress?.toString()?.uppercase(Locale.US)
                    if (mac != null) onAssociated(activity, mac, associationInfo.id)
                }

                override fun onFailure(error: CharSequence?) {
                    onError(error?.toString() ?: "Pairing failed")
                }
            })
        } else {
            @Suppress("DEPRECATION")
            cdm.associate(request, object : CompanionDeviceManager.Callback() {
                @Deprecated("Pre-API 33 path")
                override fun onDeviceFound(chooserLauncher: IntentSender) {
                    launchChooser(activity, chooserLauncher, onError)
                }

                override fun onFailure(error: CharSequence?) {
                    onError(error?.toString() ?: "Pairing failed")
                }
            }, null)
        }
    }

    private fun launchChooser(activity: Activity, sender: IntentSender, onError: (String) -> Unit) {
        try {
            activity.startIntentSenderForResult(sender, REQUEST_ASSOCIATE, null, 0, 0, 0)
        } catch (e: IntentSender.SendIntentException) {
            onError("Could not open the pairing dialog")
        }
    }

    /** Handle the chooser result from onActivityResult. Returns true if a watch was associated. */
    fun handleResult(activity: Activity, resultCode: Int, data: Intent?): Boolean {
        if (resultCode != Activity.RESULT_OK || data == null) return false
        var address: String? = null
        var id = -1
        if (Build.VERSION.SDK_INT >= 33) {
            data.getParcelableExtra(CompanionDeviceManager.EXTRA_ASSOCIATION, AssociationInfo::class.java)
                ?.let {
                    id = it.id
                    address = it.deviceMacAddress?.toString()
                }
        }
        if (address == null) {
            @Suppress("DEPRECATION")
            val device: Parcelable? = data.getParcelableExtra(CompanionDeviceManager.EXTRA_DEVICE)
            address = when (device) {
                is ScanResult -> device.device.address
                is BluetoothDevice -> device.address
                else -> null
            }
        }
        val mac = address?.uppercase(Locale.US) ?: return false
        onAssociated(activity, mac, id)
        return true
    }

    private fun onAssociated(context: Context, mac: String, associationId: Int) {
        val prefs = Prefs(context)
        val changed = prefs.watchAddress != mac
        prefs.watchAddress = mac
        if (associationId >= 0) prefs.associationId = associationId
        if (!changed) return
        Log.i(TAG, "Associated with $mac (id $associationId)")
        startObserving(context, mac)
        WatchState.update(WatchState.Link.WAITING, "Waiting for watch")
        WatchLinkService.start(context)
    }

    fun startObserving(context: Context, mac: String) {
        if (Build.VERSION.SDK_INT < 31) return
        try {
            context.getSystemService(CompanionDeviceManager::class.java)
                ?.startObservingDevicePresence(mac)
        } catch (e: Exception) {
            Log.w(TAG, "startObservingDevicePresence failed", e)
        }
    }

    /** What happened to the Bluetooth bond when the watch was forgotten. */
    enum class BondRemoval { NOT_BONDED, REMOVED, FAILED }

    /**
     * Forget the watch: stop the link, remove the Bluetooth bond (if the secure link was
     * used), stop presence observation and remove the CDM association. If the result is
     * [BondRemoval.FAILED] the user has to remove the watch in Android's Bluetooth settings.
     */
    fun disassociate(context: Context): BondRemoval {
        val prefs = Prefs(context)
        val mac = prefs.watchAddress
        val cdm = context.getSystemService(CompanionDeviceManager::class.java)
        WatchLinkService.stop(context)
        // Before the association goes: newer Android versions only let a companion app
        // unbond a device it is associated with.
        val bond = if (mac != null) removeBond(context, mac) else BondRemoval.NOT_BONDED
        if (cdm != null) {
            if (Build.VERSION.SDK_INT >= 31 && mac != null) {
                try {
                    cdm.stopObservingDevicePresence(mac)
                } catch (e: Exception) {
                    Log.w(TAG, "stopObservingDevicePresence failed", e)
                }
            }
            try {
                if (Build.VERSION.SDK_INT >= 33 && prefs.associationId >= 0) {
                    cdm.disassociate(prefs.associationId)
                } else if (mac != null) {
                    @Suppress("DEPRECATION")
                    cdm.disassociate(mac)
                }
            } catch (e: Exception) {
                Log.w(TAG, "disassociate failed", e)
            }
        }
        prefs.clearWatch()
        WatchState.update(WatchState.Link.NOT_PAIRED)
        return bond
    }

    /**
     * There is no public API to remove a bond; `BluetoothDevice.removeBond()` is hidden, so it
     * is called by reflection and any failure is reported instead of thrown.
     */
    @SuppressLint("MissingPermission", "DiscouragedPrivateApi", "PrivateApi")
    private fun removeBond(context: Context, mac: String): BondRemoval {
        return try {
            val adapter = context.getSystemService(BluetoothManager::class.java)?.adapter
                ?: return BondRemoval.FAILED
            val device = adapter.getRemoteDevice(mac)
            if (device.bondState == BluetoothDevice.BOND_NONE) return BondRemoval.NOT_BONDED
            val removed = BluetoothDevice::class.java.getMethod("removeBond").invoke(device) as? Boolean
            if (removed == true) BondRemoval.REMOVED else BondRemoval.FAILED
        } catch (e: Exception) {
            Log.w(TAG, "removeBond failed", e)
            BondRemoval.FAILED
        }
    }

    /** After a reinstall/data clear the system may still hold our association; adopt it. */
    fun adoptExistingAssociation(context: Context) {
        val prefs = Prefs(context)
        if (prefs.isPaired) return
        val cdm = context.getSystemService(CompanionDeviceManager::class.java) ?: return
        try {
            if (Build.VERSION.SDK_INT >= 33) {
                val info = cdm.myAssociations.singleOrNull { it.deviceMacAddress != null } ?: return
                prefs.watchAddress = info.deviceMacAddress.toString().uppercase(Locale.US)
                prefs.associationId = info.id
            } else {
                @Suppress("DEPRECATION")
                val mac = cdm.associations.singleOrNull() ?: return
                prefs.watchAddress = mac.uppercase(Locale.US)
            }
            prefs.watchAddress?.let { startObserving(context, it) }
        } catch (e: Exception) {
            Log.w(TAG, "Could not read existing associations", e)
        }
    }
}
