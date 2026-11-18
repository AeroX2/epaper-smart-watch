package com.james.epaperwatch.ui

import android.Manifest
import android.app.Activity
import android.app.AlertDialog
import android.content.ActivityNotFoundException
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Typeface
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import android.text.format.DateUtils
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.RadioButton
import android.widget.RadioGroup
import android.widget.ScrollView
import android.widget.Switch
import android.widget.TextView
import android.widget.Toast
import com.james.epaperwatch.R
import com.james.epaperwatch.WatchLinkService
import com.james.epaperwatch.WatchNotificationListener
import com.james.epaperwatch.bridge.CalendarBridge
import com.james.epaperwatch.bridge.PhoneLocation
import com.james.epaperwatch.data.Prefs
import com.james.epaperwatch.data.WatchState
import com.james.epaperwatch.health.StepSync
import com.james.epaperwatch.link.GattLink
import com.james.epaperwatch.pairing.CompanionPairing
import com.james.epaperwatch.protocol.Out
import com.james.epaperwatch.protocol.Protocol

/** Watch status, pairing, and every user-facing setting. */
class MainActivity : Activity() {
    private lateinit var ui: Ui
    private lateinit var prefs: Prefs

    private lateinit var dial: StatusDialView
    private lateinit var statusLine: TextView
    private lateinit var batteryValue: TextView
    private lateinit var voltageValue: TextView
    private lateinit var chargingValue: TextView
    private lateinit var firmwareValue: TextView
    private lateinit var secureValue: TextView
    private lateinit var otaValue: TextView
    private lateinit var storageValue: TextView
    private lateinit var stepsValue: TextView
    private lateinit var syncValue: TextView
    private lateinit var history: HistoryChartView
    private lateinit var pairButton: Button
    private lateinit var watchActions: View

    private lateinit var vibrationSwitch: Switch
    private lateinit var quietSwitch: Switch
    private lateinit var toneButton: Button
    private lateinit var healthSwitch: Switch
    private lateinit var healthStatus: TextView
    private lateinit var sleepStatus: TextView
    private lateinit var backgroundLocationButton: Button
    private lateinit var accessButton: Button
    private lateinit var forwardSwitch: Switch
    private lateinit var redactSwitch: Switch
    private lateinit var messagesOnlySwitch: Switch
    private lateinit var callsSwitch: Switch
    private lateinit var rejectSwitch: Switch
    private lateinit var calendarSwitch: Switch

    private lateinit var weatherModes: RadioGroup
    private lateinit var weatherStatus: TextView
    private lateinit var latInput: EditText
    private lateinit var lonInput: EditText
    private lateinit var coordsBlock: View
    private lateinit var manualBlock: View
    private lateinit var tempInput: EditText
    private lateinit var highInput: EditText
    private lateinit var lowInput: EditText
    private lateinit var conditionInput: EditText

    /** Set while render() updates widgets so their listeners don't act. */
    private var rendering = false
    private val stateListener: () -> Unit = { render() }
    private val mainHandler = Handler(Looper.getMainLooper())

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        ui = Ui(this)
        prefs = Prefs(this)
        window.statusBarColor = Ui.PAPER
        window.navigationBarColor = Ui.PAPER
        CompanionPairing.adoptExistingAssociation(this)
        setContentView(build())
        WatchLinkService.start(this)
    }

    override fun onStart() {
        super.onStart()
        WatchState.addListener(stateListener)
    }

    override fun onResume() {
        super.onResume()
        // Returning from system settings (notification access, permissions) may change things.
        if (WatchNotificationListener.isEnabled(this)) WatchLinkService.instance?.onNotificationAccessGranted()
        refreshLocationFix()
        render()
    }

    override fun onStop() {
        WatchState.removeListener(stateListener)
        super.onStop()
    }

    @Deprecated("Platform Activity result API")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        @Suppress("DEPRECATION")
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == CompanionPairing.REQUEST_ASSOCIATE) {
            if (CompanionPairing.handleResult(this, resultCode, data)) toast("Watch paired")
            render()
        } else if (requestCode == REQ_HEALTH) {
            if (StepSync.permissionGranted(resultCode, data)) {
                enableHealthSync()
            } else {
                toast("Health Connect did not grant permission to write steps")
                render()
            }
        }
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray,
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        val granted = grantResults.isNotEmpty() && grantResults.all { it == PackageManager.PERMISSION_GRANTED }
        when (requestCode) {
            REQ_PAIR -> {
                if (GattLink.hasConnectPermission(this)) {
                    if (prefs.isPaired) WatchLinkService.reconnect(this) else pair()
                } else {
                    toast("Nearby devices permission is needed to talk to the watch")
                }
            }
            REQ_CALENDAR -> {
                prefs.calendarEnabled = granted
                if (!granted) toast("Calendar permission denied")
                WatchLinkService.instance?.onCalendarSettingChanged()
            }
            REQ_CALLS -> {
                prefs.rejectCalls = granted
                if (!granted) toast("Without this permission the watch uses the call's Decline button")
            }
            REQ_LOCATION -> {
                if (granted) {
                    askBackgroundLocation(force = false)
                    refreshLocationFix()
                } else {
                    toast("Location denied: use fixed coordinates or manual values instead")
                }
                WatchLinkService.instance?.refreshWeather()
            }
            REQ_HEALTH_RUNTIME -> {
                if (granted) enableHealthSync() else healthPermissionRefused()
            }
            REQ_BACKGROUND_LOCATION -> {
                if (!PhoneLocation.hasBackgroundPermission(this)) {
                    toast("Without it the weather location is only refreshed while this app is open")
                }
                WatchLinkService.instance?.refreshWeather()
            }
        }
        render()
    }

    // ------------------------------------------------------------------ actions

    private fun onPairClicked() {
        val needed = mutableListOf<String>()
        if (Build.VERSION.SDK_INT >= 31) needed += Manifest.permission.BLUETOOTH_CONNECT
        if (Build.VERSION.SDK_INT >= 33) needed += Manifest.permission.POST_NOTIFICATIONS
        val missing = needed.filter { checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED }
        if (missing.isNotEmpty()) {
            requestPermissions(missing.toTypedArray(), REQ_PAIR)
        } else if (prefs.isPaired) {
            WatchLinkService.reconnect(this) // also ends a backoff wait or a given-up link
        } else {
            pair()
        }
    }

    private fun pair() {
        CompanionPairing.startAssociation(this) { error -> toast(error) }
    }

    private fun forget() {
        AlertDialog.Builder(this)
            .setTitle("Forget this watch?")
            .setMessage(
                "The phone stops connecting to it and removes its Bluetooth pairing. " +
                    "You can pair again at any time.",
            )
            .setPositiveButton("Forget") { _, _ ->
                val bond = CompanionPairing.disassociate(this)
                render()
                if (bond == CompanionPairing.BondRemoval.FAILED) {
                    AlertDialog.Builder(this)
                        .setTitle("Remove the Bluetooth pairing")
                        .setMessage(
                            "The app could not remove the watch's Bluetooth pairing. Open " +
                                "Android's Bluetooth settings and forget \"E-Paper Watch\" " +
                                "there, otherwise pairing it again can fail.",
                        )
                        .setPositiveButton("Bluetooth settings") { _, _ ->
                            try {
                                startActivity(Intent(Settings.ACTION_BLUETOOTH_SETTINGS))
                            } catch (_: ActivityNotFoundException) {
                            }
                        }
                        .setNegativeButton("Later", null)
                        .show()
                }
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    /**
     * Runs a watch command with visible progress: the button shows [working] and is disabled
     * until the watch acknowledges, then a toast says [done] or why it failed.
     */
    private fun watchAction(
        button: Button,
        working: String,
        done: String,
        action: (WatchLinkService, (Boolean, String?) -> Unit) -> Unit,
    ) {
        val service = WatchLinkService.instance
        if (service == null || !service.sessionActive) {
            toast("Watch is not connected")
            return
        }
        val label = button.text
        ui.setBusy(button, label, working)
        action(service) { ok, why ->
            if (isDestroyed) return@action
            ui.setBusy(button, label, null)
            toast(if (ok) done else why ?: "That did not work")
            render()
        }
    }

    /** A setting changed on the phone: send it, or say why the watch does not have it yet. */
    private fun pushSettings() {
        val service = WatchLinkService.instance
        if (service != null && service.sessionActive) {
            service.sendSettings()
        } else {
            toast("Saved on the phone. The watch is not connected.")
        }
    }

    private fun saveWeather() {
        val mode = when (weatherModes.checkedRadioButtonId) {
            ID_LOCATION -> Prefs.WEATHER_LOCATION
            ID_COORDS -> Prefs.WEATHER_COORDS
            ID_MANUAL -> Prefs.WEATHER_MANUAL
            else -> Prefs.WEATHER_OFF
        }
        prefs.weatherMode = mode
        prefs.weatherLat = latInput.text.toString().trim()
        prefs.weatherLon = lonInput.text.toString().trim()
        prefs.manualTemp = tempInput.text.toString().toIntOrNull() ?: prefs.manualTemp
        prefs.manualHigh = highInput.text.toString().toIntOrNull() ?: prefs.manualHigh
        prefs.manualLow = lowInput.text.toString().toIntOrNull() ?: prefs.manualLow
        prefs.manualCondition = conditionInput.text.toString()
        prefs.weatherError = null
        if (mode == Prefs.WEATHER_LOCATION && !PhoneLocation.hasPermission(this)) {
            requestPermissions(arrayOf(Manifest.permission.ACCESS_COARSE_LOCATION), REQ_LOCATION)
        } else {
            if (mode == Prefs.WEATHER_LOCATION) {
                askBackgroundLocation(force = false)
                refreshLocationFix()
            }
            WatchLinkService.instance?.refreshWeather()
            toast("Weather settings saved")
        }
        render()
    }

    // ------------------------------------------------------------------ alert tone

    private fun pickTone() {
        val names = Protocol.TONE_NAMES.toTypedArray()
        AlertDialog.Builder(this)
            .setTitle("Notification alert tone")
            .setSingleChoiceItems(names, prefs.tone) { dialog, which ->
                prefs.tone = which
                val service = WatchLinkService.instance
                if (service != null && service.sessionActive) {
                    service.sendSettings()
                    service.previewTone(which) // lets the wearer hear it; "None" has no preview
                } else {
                    toast("Saved on the phone. The watch is not connected.")
                }
                dialog.dismiss()
                render()
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    // ------------------------------------------------------------------ weather location

    /**
     * Android 10+: the link service counts as "background" for location, so weather by phone
     * location only keeps working with the app closed if background location is allowed.
     * Explains that first, then asks. Declining is fine: the service then uses the fix this
     * screen caches whenever it is opened (see [refreshLocationFix]).
     */
    private fun askBackgroundLocation(force: Boolean) {
        if (Build.VERSION.SDK_INT < 29) return
        if (!PhoneLocation.hasPermission(this) || PhoneLocation.hasBackgroundPermission(this)) return
        if (prefs.backgroundLocationAsked && !force) return
        prefs.backgroundLocationAsked = true
        val option = if (Build.VERSION.SDK_INT >= 30) {
            "\"${packageManager.backgroundPermissionOptionLabel}\""
        } else {
            "\"Allow all the time\""
        }
        AlertDialog.Builder(this)
            .setTitle("Weather while the app is closed")
            .setMessage(
                "The watch link runs in the background. Android only lets it read the " +
                    "phone's approximate location there if location access is set to $option.\n\n" +
                    "The location is used for nothing but the weather forecast request. If you " +
                    "skip this, weather still works, but the location is only refreshed while " +
                    "this app is open.",
            )
            .setPositiveButton("Continue") { _, _ ->
                requestPermissions(
                    arrayOf(Manifest.permission.ACCESS_BACKGROUND_LOCATION), REQ_BACKGROUND_LOCATION,
                )
            }
            .setNegativeButton("Not now", null)
            .show()
    }

    /** The UI is visible, so a location fix is allowed: refresh the cached one. */
    private fun refreshLocationFix() {
        if (prefs.weatherMode != Prefs.WEATHER_LOCATION || !PhoneLocation.hasPermission(this)) return
        PhoneLocation.obtain(this, mainHandler, UI_FIX_MAX_AGE_MS) {
            WatchLinkService.instance?.refreshWeather(force = false)
            if (!isDestroyed) render()
        }
    }

    // ------------------------------------------------------------------ Health Connect

    private fun onHealthSwitch(view: Switch, on: Boolean) {
        if (!on) {
            prefs.healthSync = false
            render()
            return
        }
        view.isChecked = false // stays off until Health Connect is there and permission is granted
        when (StepSync.availability(this)) {
            StepSync.Availability.UNAVAILABLE -> toast("Health Connect is not available on this phone")
            StepSync.Availability.UPDATE_REQUIRED -> AlertDialog.Builder(this)
                .setTitle("Health Connect needed")
                .setMessage("Install or update the Health Connect app, then switch this on again.")
                .setPositiveButton("Open Play Store") { _, _ ->
                    try {
                        startActivity(StepSync.installIntent(this))
                    } catch (_: ActivityNotFoundException) {
                        toast("Could not open the Play Store")
                    }
                }
                .setNegativeButton("Cancel", null)
                .show()
            StepSync.Availability.AVAILABLE -> StepSync.checkPermission(this) { granted ->
                if (isDestroyed) return@checkPermission
                if (granted) {
                    enableHealthSync()
                } else if (Build.VERSION.SDK_INT >= 34) {
                    // Health permissions are runtime permissions from Android 14; the
                    // library's intent only works through ComponentActivity there.
                    requestPermissions(arrayOf(StepSync.WRITE_STEPS), REQ_HEALTH_RUNTIME)
                } else {
                    try {
                        @Suppress("DEPRECATION")
                        startActivityForResult(StepSync.permissionIntent(this), REQ_HEALTH)
                    } catch (e: Exception) {
                        toast("Could not open the Health Connect permission screen")
                    }
                }
            }
        }
    }

    private fun enableHealthSync() {
        prefs.healthSync = true
        prefs.healthStatus = null
        StepSync.syncNow(this)
        toast("Step sync is on")
        render()
    }

    /** Android 14+: after two refusals the system stops asking, so offer the settings page. */
    private fun healthPermissionRefused() {
        AlertDialog.Builder(this)
            .setTitle("Permission to write steps")
            .setMessage(
                "Health Connect did not give this app permission to write steps. You can " +
                    "allow it in Health Connect, under this app's permissions, then switch " +
                    "step sync on again.",
            )
            .setPositiveButton("Open Health Connect") { _, _ ->
                try {
                    startActivity(StepSync.settingsIntent(this))
                } catch (_: Exception) {
                    toast("Open Settings > Health Connect > App permissions")
                }
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun sleepSummary(): String = when {
        !WatchState.sleepTracking -> "Not tracking."
        WatchState.sleepSamples == 0 ->
            "Tracking: waiting for the first movement sample from the watch (about 10 seconds)."
        else -> "Tracking: ${WatchState.sleepSamples} movement samples received, " +
            "last ${WatchState.sleepLastMg} mg."
    }

    private fun healthSummary(): String {
        if (!prefs.healthSync) return "Off. When on, the watch's daily step counts are written to Health Connect."
        val status = prefs.healthStatus
        val last = if (prefs.healthLastSync > 0) {
            "Last sync " + DateUtils.getRelativeTimeSpanString(
                prefs.healthLastSync, System.currentTimeMillis(), DateUtils.MINUTE_IN_MILLIS,
            )
        } else {
            "Not synced yet (waiting for steps from the watch)"
        }
        return if (status != null) "$last · $status" else last
    }

    private fun flagText(bit: Int, yes: String, no: String): String {
        val flags = prefs.watchFlags
        return when {
            flags < 0 -> "—"
            flags and bit != 0 -> yes
            else -> no
        }
    }

    // ------------------------------------------------------------------ rendering

    private fun render() {
        rendering = true
        val paired = prefs.isPaired
        val reported = WatchState.link
        // Before the service reports in, a paired watch is simply "waiting".
        val stale = paired && reported == WatchState.Link.NOT_PAIRED
        val link = if (stale) WatchState.Link.WAITING else reported
        val connected = link == WatchState.Link.CONNECTED

        dial.connected = connected
        dial.battery = if (paired) prefs.watchBattery else -1
        dial.headline = when {
            !paired -> "PAIR"
            prefs.watchBattery >= 0 -> "${prefs.watchBattery}%"
            connected -> "LINK"
            else -> "WAIT"
        }
        dial.caption = if (paired) link.label else "No watch"
        dial.invalidate()
        statusLine.text = when {
            !paired -> "Pair your watch to get started."
            !GattLink.hasConnectPermission(this) -> "Grant Nearby devices permission to connect."
            stale -> link.label
            else -> WatchState.detail.ifEmpty { link.label }
        }

        batteryValue.text = if (prefs.watchBattery >= 0) "${prefs.watchBattery}%" else "—"
        voltageValue.text = if (prefs.watchMillivolts > 0) "${prefs.watchMillivolts} mV" else "—"
        chargingValue.text = when (prefs.watchCharging) {
            1 -> "Charging"
            0 -> "On battery"
            else -> "Unknown"
        }
        firmwareValue.text = prefs.firmware?.ifEmpty { null } ?: "—"
        secureValue.text = flagText(Protocol.FLAG_SECURE, "Encrypted (bonded)", "Not encrypted")
        otaValue.text = flagText(Protocol.FLAG_OTA, "Supported", "Not supported")
        storageValue.text = flagText(Protocol.FLAG_STORAGE, "Working", "Not working")
        stepsValue.text = if (prefs.watchSteps >= 0) "%,d".format(prefs.watchSteps) else "—"
        syncValue.text = if (prefs.lastSync > 0) {
            DateUtils.getRelativeTimeSpanString(
                prefs.lastSync, System.currentTimeMillis(), DateUtils.MINUTE_IN_MILLIS,
            )
        } else {
            "Never"
        }
        history.setData(prefs.history)

        pairButton.text = when {
            !paired -> "Pair watch"
            !GattLink.hasConnectPermission(this) -> "Grant Nearby devices permission"
            else -> "Reconnect"
        }
        pairButton.visibility = if (paired && connected) View.GONE else View.VISIBLE
        watchActions.visibility = if (paired) View.VISIBLE else View.GONE

        vibrationSwitch.isChecked = prefs.vibration
        quietSwitch.isChecked = prefs.quiet
        toneButton.text = "Alert tone: " +
            (Protocol.TONE_NAMES.getOrNull(prefs.tone) ?: "watch default")
        healthSwitch.isChecked = prefs.healthSync
        healthStatus.text = healthSummary()
        sleepStatus.text = sleepSummary()
        val access = WatchNotificationListener.isEnabled(this)
        accessButton.text = if (access) "Notification access: granted" else "Grant notification access"
        accessButton.setTextColor(if (access) Ui.INK else Ui.ALERT)
        forwardSwitch.isChecked = prefs.forwardNotifications
        redactSwitch.isChecked = prefs.redactCodes
        messagesOnlySwitch.isChecked = prefs.messagesOnly
        callsSwitch.isChecked = prefs.forwardCalls
        rejectSwitch.isChecked = prefs.rejectCalls && hasCallPermission()
        rejectSwitch.isEnabled = Build.VERSION.SDK_INT >= 28
        calendarSwitch.isChecked = prefs.calendarEnabled && CalendarBridge.hasPermission(this)

        coordsBlock.visibility = if (weatherModes.checkedRadioButtonId == ID_COORDS) View.VISIBLE else View.GONE
        manualBlock.visibility = if (weatherModes.checkedRadioButtonId == ID_MANUAL) View.VISIBLE else View.GONE
        weatherStatus.text = weatherSummary()
        backgroundLocationButton.visibility =
            if (weatherModes.checkedRadioButtonId == ID_LOCATION && Build.VERSION.SDK_INT >= 29 &&
                PhoneLocation.hasPermission(this) && !PhoneLocation.hasBackgroundPermission(this)
            ) View.VISIBLE else View.GONE
        rendering = false
    }

    private fun weatherSummary(): String {
        if (prefs.weatherMode == Prefs.WEATHER_OFF) return "Weather is off."
        if (prefs.weatherMode == Prefs.WEATHER_MANUAL) {
            return "Manual: ${prefs.manualTemp}°C (${prefs.manualHigh}/${prefs.manualLow}) ${prefs.manualCondition}"
        }
        val cache = prefs.weatherCache?.split('|')
        val base = if (cache != null && cache.size >= 5) {
            val age = DateUtils.getRelativeTimeSpanString(
                prefs.weatherCacheTime, System.currentTimeMillis(), DateUtils.MINUTE_IN_MILLIS,
            )
            "${cache[0]}°C (${cache[1]}/${cache[2]}) ${cache[4]} · $age"
        } else {
            "No weather fetched yet."
        }
        var summary = prefs.weatherError?.let { "$base\n$it" } ?: base
        if (prefs.weatherMode == Prefs.WEATHER_LOCATION && PhoneLocation.hasPermission(this)) {
            val fix = PhoneLocation.cached(this)
            if (fix != null) {
                summary += "\nLocation fix: " + DateUtils.getRelativeTimeSpanString(
                    fix.timeMs, System.currentTimeMillis(), DateUtils.MINUTE_IN_MILLIS,
                )
            }
            if (!PhoneLocation.hasBackgroundPermission(this)) {
                summary += "\nBackground location is not allowed: the location is only " +
                    "refreshed while this app is open."
            }
        }
        return summary
    }

    private fun hasCallPermission() = Build.VERSION.SDK_INT >= 28 &&
        checkSelfPermission(Manifest.permission.ANSWER_PHONE_CALLS) == PackageManager.PERMISSION_GRANTED

    private fun toast(message: String) = Toast.makeText(this, message, Toast.LENGTH_LONG).show()

    // ------------------------------------------------------------------ layout

    private fun build(): View {
        val root = ui.column().apply {
            setPadding(ui.dp(22), ui.dp(24), ui.dp(22), ui.dp(40))
            setBackgroundColor(Ui.PAPER)
        }
        root.addView(ui.eyebrow("COMPANION / PROTOCOL V2"))
        root.addView(ui.label("E-Paper Watch", 32).apply {
            setTypeface(Typeface.create("sans-serif-condensed", Typeface.BOLD))
            setPadding(0, ui.dp(4), 0, ui.dp(16))
        })

        // ---- Status ----
        dial = StatusDialView(this)
        root.addView(dial, LinearLayout.LayoutParams(ui.dp(220), ui.dp(220)).apply {
            gravity = Gravity.CENTER_HORIZONTAL
        })
        statusLine = ui.label("", 13, Ui.INK, mono = true).apply {
            gravity = Gravity.CENTER
            setPadding(0, ui.dp(10), 0, ui.dp(12))
        }
        root.addView(statusLine)
        pairButton = ui.button("Pair watch") { onPairClicked() }
        root.addView(pairButton)

        val details = ui.column().apply { setPadding(0, ui.dp(8), 0, 0) }
        batteryValue = ui.keyValue(details, "Watch battery")
        voltageValue = ui.keyValue(details, "Voltage")
        chargingValue = ui.keyValue(details, "Power")
        firmwareValue = ui.keyValue(details, "Firmware")
        secureValue = ui.keyValue(details, "Link security")
        otaValue = ui.keyValue(details, "Bluetooth update")
        storageValue = ui.keyValue(details, "Watch storage")
        stepsValue = ui.keyValue(details, "Steps today")
        syncValue = ui.keyValue(details, "Last sync")
        root.addView(details)

        root.addView(ui.section("ACTIVITY", "Last 7 days"))
        history = HistoryChartView(this)
        root.addView(history, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ui.dp(130)))

        watchActions = ui.column().apply {
            setPadding(0, ui.dp(12), 0, 0)
            addView(ui.row(
                ui.actionButton("Find watch") { button ->
                    watchAction(button, "Calling watch…", "The watch is ringing") { service, done ->
                        service.findWatch(done)
                    }
                },
                ui.actionButton("Sync now", primary = false) { button ->
                    watchAction(button, "Syncing…", "Watch synced") { service, done ->
                        service.resync(done)
                    }
                },
            ))
            addView(ui.row(
                ui.actionButton("Refresh status", primary = false) { button ->
                    watchAction(button, "Refreshing…", "Status updated") { service, done ->
                        service.requestStatusAndHistory(done)
                    }
                },
                ui.button("Forget watch", primary = false) { forget() },
            ))
            addView(ui.button("Update watch firmware", primary = false) {
                startActivity(Intent(this@MainActivity, FirmwareUpdateActivity::class.java))
            })
        }
        root.addView(watchActions)

        // ---- Watch settings ----
        root.addView(ui.section("WATCH", "Alerts"))
        vibrationSwitch = ui.switchRow("Vibration") { _, on ->
            if (rendering) return@switchRow
            prefs.vibration = on
            pushSettings()
        }
        quietSwitch = ui.switchRow("Quiet mode", "No buzzer or vibration for alerts") { _, on ->
            if (rendering) return@switchRow
            prefs.quiet = on
            pushSettings()
        }
        root.addView(vibrationSwitch)
        root.addView(quietSwitch)
        toneButton = ui.button("Alert tone", primary = false) { pickTone() }
        root.addView(toneButton)
        root.addView(ui.note("The tone the watch plays for a new notification. Quiet mode silences it."))

        // ---- Health Connect ----
        root.addView(ui.section("HEALTH CONNECT", "Step sync"))
        healthSwitch = ui.switchRow(
            "Sync steps to Health Connect", "One record per day from the watch's step counter",
        ) { view, on ->
            if (rendering) return@switchRow
            onHealthSwitch(view, on)
        }
        root.addView(healthSwitch)
        healthStatus = ui.note("")
        root.addView(healthStatus)

        // ---- Notifications ----
        root.addView(ui.section("NOTIFICATIONS", "What reaches your wrist"))
        accessButton = ui.button("Grant notification access", primary = false) {
            startActivity(Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS))
        }
        root.addView(accessButton)
        root.addView(ui.note("Notification access is also what lets the watch see and control media."))
        forwardSwitch = ui.switchRow("Forward notifications") { _, on ->
            if (rendering) return@switchRow
            prefs.forwardNotifications = on
            WatchNotificationListener.reevaluate()
        }
        root.addView(forwardSwitch)
        root.addView(ui.button("Choose apps and private apps", primary = false) {
            startActivity(Intent(this, AppFilterActivity::class.java))
        })
        messagesOnlySwitch = ui.switchRow(
            "Only messages and email",
            "Chats, texts, email and calendar reminders. Off: every app",
        ) { _, on ->
            if (rendering) return@switchRow
            prefs.messagesOnly = on
            WatchNotificationListener.reevaluate()
        }
        root.addView(messagesOnlySwitch)
        redactSwitch = ui.switchRow("Hide one-time codes", "Masks 4–8 digit codes next to words like code, OTP or PIN") { _, on ->
            if (rendering) return@switchRow
            prefs.redactCodes = on
            WatchNotificationListener.reevaluate()
        }
        root.addView(redactSwitch)
        callsSwitch = ui.switchRow("Incoming calls", "Show the caller on the watch") { _, on ->
            if (rendering) return@switchRow
            prefs.forwardCalls = on
        }
        root.addView(callsSwitch)
        rejectSwitch = ui.switchRow("Reject calls from the watch", "Needs the phone-call permission (Android 9+)") { view, on ->
            if (rendering) return@switchRow
            if (on && Build.VERSION.SDK_INT >= 28 && !hasCallPermission()) {
                view.isChecked = false
                requestPermissions(arrayOf(Manifest.permission.ANSWER_PHONE_CALLS), REQ_CALLS)
            } else {
                prefs.rejectCalls = on
            }
        }
        root.addView(rejectSwitch)

        // ---- Calendar ----
        root.addView(ui.section("CALENDAR", "Next event"))
        calendarSwitch = ui.switchRow("Show next calendar event", "Next event starting within 24 hours") { view, on ->
            if (rendering) return@switchRow
            if (on && !CalendarBridge.hasPermission(this)) {
                view.isChecked = false
                requestPermissions(arrayOf(Manifest.permission.READ_CALENDAR), REQ_CALENDAR)
            } else {
                prefs.calendarEnabled = on
                WatchLinkService.instance?.onCalendarSettingChanged()
            }
        }
        root.addView(calendarSwitch)

        // ---- Weather ----
        root.addView(ui.section("WEATHER", "Open-Meteo forecast"))
        weatherModes = RadioGroup(this).apply {
            orientation = RadioGroup.VERTICAL
            addView(radio(ID_OFF, "Off"))
            addView(radio(ID_LOCATION, "Use phone location (approximate)"))
            addView(radio(ID_COORDS, "Fixed coordinates"))
            addView(radio(ID_MANUAL, "Manual values (no internet)"))
            check(
                when (prefs.weatherMode) {
                    Prefs.WEATHER_LOCATION -> ID_LOCATION
                    Prefs.WEATHER_COORDS -> ID_COORDS
                    Prefs.WEATHER_MANUAL -> ID_MANUAL
                    else -> ID_OFF
                },
            )
            setOnCheckedChangeListener { _, _ -> if (!rendering) render() }
        }
        root.addView(weatherModes)
        latInput = ui.textInput(prefs.weatherLat, numeric = true, decimal = true)
        lonInput = ui.textInput(prefs.weatherLon, numeric = true, decimal = true)
        coordsBlock = ui.row(ui.field("LATITUDE", latInput), ui.field("LONGITUDE", lonInput))
        root.addView(coordsBlock)
        tempInput = ui.textInput(prefs.manualTemp.toString(), numeric = true)
        highInput = ui.textInput(prefs.manualHigh.toString(), numeric = true)
        lowInput = ui.textInput(prefs.manualLow.toString(), numeric = true)
        conditionInput = ui.textInput(prefs.manualCondition)
        manualBlock = ui.column().apply {
            addView(ui.row(
                ui.field("NOW °C", tempInput),
                ui.field("HIGH °C", highInput),
                ui.field("LOW °C", lowInput),
            ))
            addView(ui.field("CONDITION", conditionInput))
        }
        root.addView(manualBlock)
        weatherStatus = ui.note("")
        root.addView(weatherStatus)
        backgroundLocationButton = ui.button("Allow background location", primary = false) {
            askBackgroundLocation(force = true)
        }
        root.addView(backgroundLocationButton)
        root.addView(ui.button("Save and update weather") { saveWeather() })

        // ---- Sleep as Android ----
        root.addView(ui.section("SLEEP AS ANDROID", "Movement-only wearable bridge"))
        root.addView(ui.note(
            "Sleep as Android starts and stops tracking automatically through its wearable " +
                "intents. It must whitelist com.james.epaperwatch. These buttons exercise the " +
                "watch's movement sampler without a real session.",
        ))
        root.addView(ui.row(
            ui.actionButton("Test tracking", primary = false) { button ->
                watchAction(button, "Starting…", "Tracking started: move the watch") { service, done ->
                    service.startSleepTest(done)
                }
            },
            ui.actionButton("Stop test", primary = false) { button ->
                watchAction(button, "Stopping…", "Tracking stopped") { service, done ->
                    service.stopSleepTest(done)
                }
            },
        ))
        sleepStatus = ui.note("")
        root.addView(sleepStatus)

        return ScrollView(this).apply {
            isFillViewport = true
            setBackgroundColor(Ui.PAPER)
            addView(root, ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        }
    }

    private fun radio(id: Int, text: String) = RadioButton(this).apply {
        this.id = id
        this.text = text
        textSize = 15f
        setTextColor(Ui.INK)
    }

    companion object {
        private const val REQ_PAIR = 1
        private const val REQ_CALENDAR = 2
        private const val REQ_CALLS = 3
        private const val REQ_LOCATION = 4
        private const val REQ_BACKGROUND_LOCATION = 5
        private const val REQ_HEALTH_RUNTIME = 6
        private const val REQ_HEALTH = 7101
        private const val UI_FIX_MAX_AGE_MS = 10L * 60 * 1000

        private val ID_OFF = R.id.weather_off
        private val ID_LOCATION = R.id.weather_location
        private val ID_COORDS = R.id.weather_coords
        private val ID_MANUAL = R.id.weather_manual
    }
}
