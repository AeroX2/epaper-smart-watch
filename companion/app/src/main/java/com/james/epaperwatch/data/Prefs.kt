package com.james.epaperwatch.data

import android.content.Context
import android.content.SharedPreferences

/** All persisted state: pairing, user settings, filters, weather and the last watch status. */
class Prefs(context: Context) {
    private val sp: SharedPreferences =
        context.applicationContext.getSharedPreferences("watch", Context.MODE_PRIVATE)

    // ---- Pairing ----
    var watchAddress: String?
        get() = sp.getString(K_ADDRESS, null)
        set(value) = sp.edit().putString(K_ADDRESS, value).apply()
    var associationId: Int
        get() = sp.getInt(K_ASSOCIATION_ID, -1)
        set(value) = sp.edit().putInt(K_ASSOCIATION_ID, value).apply()
    val isPaired: Boolean get() = watchAddress != null

    // ---- Watch settings (phone copy wins at session start) ----
    var vibration: Boolean
        get() = sp.getBoolean("vibration", true)
        set(value) = sp.edit().putBoolean("vibration", value).apply()
    var quiet: Boolean
        get() = sp.getBoolean("quiet", false)
        set(value) = sp.edit().putBoolean("quiet", value).apply()

    /** Notification alert tone 0..5, or -1 while neither the user nor the watch has said. */
    var tone: Int
        get() = sp.getInt("tone", -1)
        set(value) = sp.edit().putInt("tone", value).apply()

    // ---- Feature toggles ----
    var forwardNotifications: Boolean
        get() = sp.getBoolean("forward_notifications", true)
        set(value) = sp.edit().putBoolean("forward_notifications", value).apply()
    var forwardCalls: Boolean
        get() = sp.getBoolean("forward_calls", true)
        set(value) = sp.edit().putBoolean("forward_calls", value).apply()
    var rejectCalls: Boolean
        get() = sp.getBoolean("reject_calls", false)
        set(value) = sp.edit().putBoolean("reject_calls", value).apply()
    var redactCodes: Boolean
        get() = sp.getBoolean("redact_codes", true)
        set(value) = sp.edit().putBoolean("redact_codes", value).apply()
    var calendarEnabled: Boolean
        get() = sp.getBoolean("calendar", false)
        set(value) = sp.edit().putBoolean("calendar", value).apply()

    // ---- Per-app notification filters ----
    var deniedApps: Set<String>
        get() = HashSet(sp.getStringSet("denied_apps", emptySet()) ?: emptySet())
        set(value) = sp.edit().putStringSet("denied_apps", HashSet(value)).apply()
    var privateApps: Set<String>
        get() = HashSet(sp.getStringSet("private_apps", emptySet()) ?: emptySet())
        set(value) = sp.edit().putStringSet("private_apps", HashSet(value)).apply()

    /** Package -> label of every app that has posted a forwardable notification. */
    val knownApps: Map<String, String>
        get() = (sp.getStringSet("known_apps", emptySet()) ?: emptySet()).associate {
            val tab = it.indexOf('\t')
            if (tab < 0) it to it else it.substring(0, tab) to it.substring(tab + 1)
        }

    fun rememberApp(packageName: String, label: String) {
        val current = sp.getStringSet("known_apps", emptySet()) ?: emptySet()
        val entry = "$packageName\t$label"
        if (entry in current) return
        val updated = current.filterNot { it.startsWith("$packageName\t") }.toHashSet()
        updated += entry
        sp.edit().putStringSet("known_apps", updated).apply()
    }

    fun setDenied(packageName: String, denied: Boolean) {
        deniedApps = deniedApps.toMutableSet().apply { if (denied) add(packageName) else remove(packageName) }
    }

    fun setPrivate(packageName: String, private: Boolean) {
        privateApps = privateApps.toMutableSet().apply { if (private) add(packageName) else remove(packageName) }
    }

    // ---- Weather ----
    var weatherMode: String
        get() = sp.getString("weather_mode", WEATHER_OFF) ?: WEATHER_OFF
        set(value) = sp.edit().putString("weather_mode", value).apply()
    var weatherLat: String
        get() = sp.getString("weather_lat", "") ?: ""
        set(value) = sp.edit().putString("weather_lat", value).apply()
    var weatherLon: String
        get() = sp.getString("weather_lon", "") ?: ""
        set(value) = sp.edit().putString("weather_lon", value).apply()
    var manualTemp: Int
        get() = sp.getInt("manual_temp", 18)
        set(value) = sp.edit().putInt("manual_temp", value).apply()
    var manualHigh: Int
        get() = sp.getInt("manual_high", 22)
        set(value) = sp.edit().putInt("manual_high", value).apply()
    var manualLow: Int
        get() = sp.getInt("manual_low", 12)
        set(value) = sp.edit().putInt("manual_low", value).apply()
    var manualCondition: String
        get() = sp.getString("manual_condition", "Partly cloudy") ?: ""
        set(value) = sp.edit().putString("manual_condition", value).apply()

    /** Last fetched weather as "temp|high|low|code|condition". */
    var weatherCache: String?
        get() = sp.getString("weather_cache", null)
        set(value) = sp.edit().putString("weather_cache", value).apply()
    var weatherCacheTime: Long
        get() = sp.getLong("weather_cache_time", 0L)
        set(value) = sp.edit().putLong("weather_cache_time", value).apply()
    var weatherError: String?
        get() = sp.getString("weather_error", null)
        set(value) = sp.edit().putString("weather_error", value).apply()

    /** Last approximate phone position as "lat|lon|epochMs" (two decimals). */
    var weatherFix: String?
        get() = sp.getString("weather_fix", null)
        set(value) = sp.edit().putString("weather_fix", value).apply()

    /** The background-location explanation has been shown once (Android 10+). */
    var backgroundLocationAsked: Boolean
        get() = sp.getBoolean("bg_location_asked", false)
        set(value) = sp.edit().putBoolean("bg_location_asked", value).apply()

    // ---- Health Connect step sync ----
    var healthSync: Boolean
        get() = sp.getBoolean("health_sync", false)
        set(value) = sp.edit().putBoolean("health_sync", value).apply()
    var healthLastSync: Long
        get() = sp.getLong("health_last_sync", 0L)
        set(value) = sp.edit().putLong("health_last_sync", value).apply()

    /** Outcome of the last Health Connect write, for the settings screen. */
    var healthStatus: String?
        get() = sp.getString("health_status", null)
        set(value) = sp.edit().putString("health_status", value).apply()

    /** Day (yyyymmdd) -> step count last written to Health Connect, newest 14 days. */
    var healthWritten: Map<String, Int>
        get() = (sp.getString("health_written", "") ?: "").split(',').mapNotNull {
            val parts = it.split(':')
            if (parts.size == 2) parts[0] to (parts[1].toIntOrNull() ?: 0) else null
        }.toMap()
        set(value) = sp.edit().putString(
            "health_written",
            value.entries.sortedBy { it.key }.takeLast(14).joinToString(",") { "${it.key}:${it.value}" },
        ).apply()

    // ---- Find phone: alarm volume to put back (-1 = nothing to restore) ----
    var findPhoneSavedVolume: Int
        get() = sp.getInt("find_phone_saved_volume", -1)
        set(value) = sp.edit().putInt("find_phone_saved_volume", value).apply()
    var findPhoneRaisedVolume: Int
        get() = sp.getInt("find_phone_raised_volume", -1)
        set(value) = sp.edit().putInt("find_phone_raised_volume", value).apply()

    // ---- Last known watch status ----
    var firmware: String?
        get() = sp.getString("fw", null)
        set(value) = sp.edit().putString("fw", value).apply()
    /** `HELLO.flags` of the last session (see Protocol.FLAG_*), -1 if never received. */
    var watchFlags: Int
        get() = sp.getInt("watch_flags", -1)
        set(value) = sp.edit().putInt("watch_flags", value).apply()
    var watchProto: Int
        get() = sp.getInt("watch_proto", 0)
        set(value) = sp.edit().putInt("watch_proto", value).apply()
    var watchBattery: Int
        get() = sp.getInt("watch_battery", -1)
        set(value) = sp.edit().putInt("watch_battery", value).apply()
    var watchMillivolts: Int
        get() = sp.getInt("watch_mv", 0)
        set(value) = sp.edit().putInt("watch_mv", value).apply()
    var watchCharging: Int
        get() = sp.getInt("watch_charging", -1)
        set(value) = sp.edit().putInt("watch_charging", value).apply()
    var watchSteps: Int
        get() = sp.getInt("watch_steps", -1)
        set(value) = sp.edit().putInt("watch_steps", value).apply()
    var lastSync: Long
        get() = sp.getLong("last_sync", 0L)
        set(value) = sp.edit().putLong("last_sync", value).apply()

    /** Up to 7 (yyyymmdd, steps) pairs, oldest first. */
    var history: List<Pair<String, Int>>
        get() = (sp.getString("history", "") ?: "").split(',').mapNotNull {
            val parts = it.split(':')
            if (parts.size == 2) parts[0] to (parts[1].toIntOrNull() ?: 0) else null
        }
        set(value) = sp.edit().putString(
            "history",
            value.takeLast(7).joinToString(",") { "${it.first}:${it.second}" },
        ).apply()

    fun clearWatch() {
        sp.edit()
            .remove(K_ADDRESS).remove(K_ASSOCIATION_ID)
            .remove("fw").remove("watch_flags").remove("watch_proto").remove("watch_battery").remove("watch_mv")
            .remove("watch_charging").remove("watch_steps").remove("last_sync").remove("history")
            .apply()
    }

    companion object {
        const val WEATHER_OFF = "off"
        const val WEATHER_LOCATION = "location"
        const val WEATHER_COORDS = "coords"
        const val WEATHER_MANUAL = "manual"

        private const val K_ADDRESS = "watch_address"
        private const val K_ASSOCIATION_ID = "association_id"
    }
}
