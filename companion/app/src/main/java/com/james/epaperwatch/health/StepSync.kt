package com.james.epaperwatch.health

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.util.Log
import androidx.health.connect.client.HealthConnectClient
import androidx.health.connect.client.PermissionController
import androidx.health.connect.client.permission.HealthPermission
import androidx.health.connect.client.records.StepsRecord
import androidx.health.connect.client.records.metadata.Device
import androidx.health.connect.client.records.metadata.Metadata
import com.james.epaperwatch.data.Prefs
import com.james.epaperwatch.data.WatchState
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import java.time.Duration
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneId

/**
 * Opt-in sync of the watch's step counts to Health Connect.
 *
 * One `StepsRecord` per local day, spanning local midnight to min(now, next midnight), upserted
 * through a stable `clientRecordId` (`epaper-watch-steps-YYYYMMDD`) with an increasing
 * `clientRecordVersion` (the write time), and attributed to a watch-type device. A day with
 * zero steps, or whose window would be empty, is never written.
 *
 * All Health Connect calls run on a background dispatcher and every failure is caught and
 * turned into the status text in prefs: nothing here can disturb the watch link.
 */
object StepSync {
    private const val TAG = "StepSync"
    private const val PROVIDER = "com.google.android.apps.healthdata"
    private const val RECORD_ID_PREFIX = "epaper-watch-steps-"

    /** STATUS carries no date: right after midnight it may still be yesterday's total. */
    private val MIDNIGHT_GUARD: Duration = Duration.ofMinutes(2)

    enum class Availability { AVAILABLE, UPDATE_REQUIRED, UNAVAILABLE }

    val WRITE_STEPS: String = HealthPermission.getWritePermission(StepsRecord::class)

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val mutex = Mutex()
    private val permissionContract = PermissionController.createRequestPermissionResultContract()

    /** Today's key and count as last written by this process (its window is never complete). */
    @Volatile private var todayWritten: Pair<String, Int>? = null

    fun availability(context: Context): Availability = try {
        when (HealthConnectClient.getSdkStatus(context, PROVIDER)) {
            HealthConnectClient.SDK_AVAILABLE -> Availability.AVAILABLE
            HealthConnectClient.SDK_UNAVAILABLE_PROVIDER_UPDATE_REQUIRED -> Availability.UPDATE_REQUIRED
            else -> Availability.UNAVAILABLE
        }
    } catch (e: RuntimeException) {
        Log.w(TAG, "getSdkStatus failed", e)
        Availability.UNAVAILABLE
    }

    /** Play Store page that installs or updates Health Connect (Android 9-13). */
    fun installIntent(context: Context): Intent =
        Intent(Intent.ACTION_VIEW).apply {
            setPackage("com.android.vending")
            data = Uri.parse("market://details?id=$PROVIDER&url=healthconnect%3A%2F%2Fonboarding")
            putExtra("overlay", true)
            putExtra("callerId", context.packageName)
        }

    /**
     * Intent of the Health Connect permission screen for writing steps. The app has no
     * `ComponentActivity`, so the library's `ActivityResultContract` is used directly:
     * `startActivityForResult(permissionIntent(..))`, then [permissionGranted] on the result.
     */
    fun permissionIntent(context: Context): Intent =
        permissionContract.createIntent(context, setOf(WRITE_STEPS))

    fun permissionGranted(resultCode: Int, data: Intent?): Boolean =
        WRITE_STEPS in permissionContract.parseResult(resultCode, data)

    /** Asks Health Connect whether the write permission is granted; answers on the main thread. */
    fun checkPermission(context: Context, callback: (Boolean) -> Unit) {
        val app = context.applicationContext
        scope.launch {
            val granted = try {
                availability(app) == Availability.AVAILABLE &&
                    WRITE_STEPS in HealthConnectClient.getOrCreate(app)
                        .permissionController.getGrantedPermissions()
            } catch (e: Exception) {
                Log.w(TAG, "Permission check failed", e)
                false
            }
            launch(Dispatchers.Main) { callback(granted) }
        }
    }

    /** STATUS: the watch's step count for today. */
    fun onStepsToday(context: Context, steps: Int) {
        val zone = ZoneId.systemDefault()
        val now = Instant.now()
        val today = LocalDate.now(zone)
        val sinceMidnight = Duration.between(today.atStartOfDay(zone).toInstant(), now)
        if (sinceMidnight < MIDNIGHT_GUARD) return
        write(context, mapOf(today to steps))
    }

    /** HISTORY: finished days (and today, last) as (yyyymmdd, steps). */
    fun onHistory(context: Context, days: List<Pair<String, Int>>) {
        val parsed = LinkedHashMap<LocalDate, Int>()
        for ((key, steps) in days) StepWindow.parseDay(key)?.let { parsed[it] = steps }
        write(context, parsed)
    }

    /** Write everything the phone currently knows (used when the option is switched on). */
    fun syncNow(context: Context) {
        val prefs = Prefs(context)
        onHistory(context, prefs.history)
        if (prefs.watchSteps > 0) onStepsToday(context, prefs.watchSteps)
    }

    private fun write(context: Context, days: Map<LocalDate, Int>) {
        val app = context.applicationContext
        val prefs = Prefs(app)
        if (!prefs.healthSync || days.isEmpty()) return
        scope.launch {
            mutex.withLock {
                try {
                    writeLocked(app, prefs, days)
                } catch (e: Exception) {
                    // SecurityException (permission revoked), RemoteException, IOException,
                    // IllegalStateException (provider missing), ...: report, never propagate.
                    Log.w(TAG, "Health Connect write failed", e)
                    report(prefs, "Failed: ${e.message ?: e.javaClass.simpleName}")
                }
            }
        }
    }

    private suspend fun writeLocked(app: Context, prefs: Prefs, days: Map<LocalDate, Int>) {
        if (!prefs.healthSync) return
        if (availability(app) != Availability.AVAILABLE) {
            report(prefs, "Health Connect is not available")
            return
        }
        val client = HealthConnectClient.getOrCreate(app)
        if (WRITE_STEPS !in client.permissionController.getGrantedPermissions()) {
            report(prefs, "Permission to write steps is missing")
            return
        }
        val zone = ZoneId.systemDefault()
        val now = Instant.now()
        val completed = prefs.healthWritten.toMutableMap()
        val device = Device(manufacturer = "DIY", model = "E-Paper Watch", type = Device.TYPE_WATCH)
        val windows = ArrayList<Pair<StepWindow, Int>>()
        for ((day, steps) in days) {
            if (!StepWindow.isWritable(steps)) continue // never a zero-step record
            val window = StepWindow.of(day, now, zone) ?: continue // never end <= start
            val unchanged = if (window.complete) {
                completed[window.key] == steps
            } else {
                todayWritten == window.key to steps
            }
            if (!unchanged) windows += window to steps
        }
        if (windows.isEmpty()) return
        val records = windows.map { (window, steps) ->
            StepsRecord(
                startTime = window.start,
                startZoneOffset = window.startOffset,
                endTime = window.end,
                endZoneOffset = window.endOffset,
                count = steps.toLong(),
                metadata = Metadata(
                    clientRecordId = RECORD_ID_PREFIX + window.key,
                    // Health Connect replaces the record with this id only for a higher version.
                    clientRecordVersion = now.toEpochMilli(),
                    device = device,
                    recordingMethod = Metadata.RECORDING_METHOD_AUTOMATICALLY_RECORDED,
                ),
            )
        }
        client.insertRecords(records)
        for ((window, steps) in windows) {
            if (window.complete) completed[window.key] = steps else todayWritten = window.key to steps
        }
        prefs.healthWritten = completed
        prefs.healthLastSync = now.toEpochMilli()
        report(prefs, if (records.size == 1) "Synced 1 day" else "Synced ${records.size} days")
    }

    private fun report(prefs: Prefs, status: String) {
        prefs.healthStatus = status
        WatchState.notifyChanged()
    }
}
