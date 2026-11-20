package com.james.epaperwatch.data

import android.content.Context
import android.content.SharedPreferences
import com.james.epaperwatch.protocol.Protocol

/** All persisted state: pairing, user settings, filters and the last watch status. */
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

    /** Button roles by corner (see [Protocol.BUTTON_CORNERS]); the default until changed. */
    var buttonLayout: List<Int>
        get() = sp.getString("button_layout", null)
            ?.split(',')?.mapNotNull { it.toIntOrNull() }
            ?.takeIf { Protocol.validButtons(it) }
            ?: Protocol.DEFAULT_BUTTONS
        set(value) = sp.edit().putString("button_layout", value.joinToString(",")).apply()

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
    /** Forward only messages, email and reminders (by Android's own category), not every app. */
    var messagesOnly: Boolean
        get() = sp.getBoolean("messages_only", true)
        set(value) = sp.edit().putBoolean("messages_only", value).apply()
    /** The watch's Camera app uses Pixel Camera (needs PixelCameraShutter) instead of our own. */
    var cameraPixel: Boolean
        get() = sp.getBoolean("camera_pixel", false)
        set(value) = sp.edit().putBoolean("camera_pixel", value).apply()
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

        private const val K_ADDRESS = "watch_address"
        private const val K_ASSOCIATION_ID = "association_id"
    }
}
