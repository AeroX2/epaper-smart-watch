package com.james.epaperwatch.bridge

import android.content.Context
import android.os.Handler
import android.os.SystemClock
import android.util.Log
import com.james.epaperwatch.data.Prefs
import com.james.epaperwatch.data.WatchState
import com.james.epaperwatch.link.PowerPolicy
import com.james.epaperwatch.protocol.Out
import com.james.epaperwatch.protocol.OutMessage
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.util.Locale
import java.util.concurrent.Executors
import kotlin.math.roundToInt

/**
 * WEATHER source using Open-Meteo (no API key). Location comes from [PhoneLocation] (the
 * phone's approximate position, cached), from fixed coordinates, or the user's manual values.
 * Results are cached in prefs.
 */
class WeatherBridge(
    private val context: Context,
    private val handler: Handler,
    /** A refresh that was started has finished (with or without a result). */
    private val onSettled: () -> Unit = {},
    private val onWeather: (OutMessage) -> Unit,
) {
    private val prefs = Prefs(context)
    private val executor = Executors.newSingleThreadExecutor()
    private var inFlight = false
    private var lastAttempt = 0L

    /** A location fix or download is under way. */
    val isBusy: Boolean get() = inFlight

    /** The best weather to send right now, or null if unknown / disabled. */
    fun currentMessage(): OutMessage? = when (prefs.weatherMode) {
        Prefs.WEATHER_MANUAL -> manualMessage()
        Prefs.WEATHER_OFF -> null
        else -> cachedMessage()
    }

    /**
     * Fetch fresh weather. Unless [force] (the user asked), nothing is fetched while the
     * cached forecast is still fresh or the last attempt was recent ([PowerPolicy]): a
     * reconnect or the hourly tick must not cost a download each time. Manual mode just
     * re-sends the values.
     */
    fun refresh(force: Boolean) {
        when (prefs.weatherMode) {
            Prefs.WEATHER_OFF -> return
            Prefs.WEATHER_MANUAL -> {
                onWeather(manualMessage())
                return
            }
        }
        if (inFlight) return
        val now = SystemClock.elapsedRealtime()
        if (!force) {
            val sinceAttempt = if (lastAttempt == 0L) null else now - lastAttempt
            val cacheAge = if (prefs.weatherCache == null) {
                Long.MAX_VALUE
            } else {
                System.currentTimeMillis() - prefs.weatherCacheTime
            }
            if (!PowerPolicy.weatherFetchDue(sinceAttempt, cacheAge)) return
        }
        lastAttempt = now
        when (prefs.weatherMode) {
            Prefs.WEATHER_COORDS -> {
                val lat = prefs.weatherLat.toDoubleOrNull()
                val lon = prefs.weatherLon.toDoubleOrNull()
                if (lat == null || lon == null || lat !in -90.0..90.0 || lon !in -180.0..180.0) {
                    fail("Enter a valid latitude and longitude")
                } else {
                    fetch(lat, lon)
                }
            }
            Prefs.WEATHER_LOCATION -> locate()
        }
    }

    fun shutdown() {
        executor.shutdownNow()
    }

    // ------------------------------------------------------------------ location

    private fun locate() {
        if (!PhoneLocation.hasPermission(context)) {
            fail("Location permission needed")
            return
        }
        inFlight = true
        PhoneLocation.obtain(context, handler, STALE_LOCATION_MS) { fix ->
            inFlight = false
            if (fix != null) {
                fetch(fix.latitude, fix.longitude)
            } else {
                if (PhoneLocation.hasBackgroundPermission(context)) {
                    fail("No location fix yet")
                } else {
                    fail("No location fix yet: open this app once so it can read the location")
                }
                onSettled()
            }
        }
    }

    // ------------------------------------------------------------------ network

    private fun fetch(latitude: Double, longitude: Double) {
        if (executor.isShutdown) return
        inFlight = true
        // Two decimals (~1 km) is plenty for a forecast and avoids sending a precise position.
        val lat = String.format(Locale.US, "%.2f", latitude)
        val lon = String.format(Locale.US, "%.2f", longitude)
        val url = "https://api.open-meteo.com/v1/forecast?latitude=$lat&longitude=$lon" +
            "&current=temperature_2m,weather_code,pressure_msl" +
            "&daily=temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=1"
        executor.execute {
            val result = try {
                Result.success(parse(download(url)))
            } catch (e: Exception) {
                Result.failure(e)
            }
            handler.post {
                inFlight = false
                result.onSuccess { (temp, high, low, code, seaLevel) ->
                    val condition = conditionFor(code)
                    prefs.weatherCache = "$temp|$high|$low|$code|$condition|$seaLevel"
                    prefs.weatherCacheTime = System.currentTimeMillis()
                    prefs.weatherError = null
                    WatchState.notifyChanged()
                    onWeather(Out.weather(temp, high, low, code, condition, seaLevel))
                }.onFailure {
                    Log.w(TAG, "Weather fetch failed", it)
                    fail("Weather fetch failed: ${it.message ?: it.javaClass.simpleName}")
                }
                onSettled()
            }
        }
    }

    private fun download(url: String): String {
        val connection = URL(url).openConnection() as HttpURLConnection
        try {
            connection.connectTimeout = 10_000
            connection.readTimeout = 10_000
            connection.setRequestProperty("Accept", "application/json")
            // Open-Meteo answers HTTP 403 to Android's default "Dalvik/..." agent.
            connection.setRequestProperty("User-Agent", USER_AGENT)
            val code = connection.responseCode
            if (code != HttpURLConnection.HTTP_OK) throw IllegalStateException("HTTP $code")
            return connection.inputStream.bufferedReader().use { it.readText() }
        } finally {
            connection.disconnect()
        }
    }

    private fun parse(json: String): List<Int> {
        val root = JSONObject(json)
        val current = root.getJSONObject("current")
        val daily = root.getJSONObject("daily")
        val temp = current.getDouble("temperature_2m").roundToInt()
        val code = current.optInt("weather_code", -1)
        val high = daily.getJSONArray("temperature_2m_max").getDouble(0).roundToInt()
        val low = daily.getJSONArray("temperature_2m_min").getDouble(0).roundToInt()
        // Sea-level pressure in tenths of a hPa, 0 when the service leaves it out. The
        // watch needs it to work out altitude from its own pressure sensor.
        val seaLevel = current.optDouble("pressure_msl", Double.NaN)
        val seaLevelTenths = if (seaLevel.isNaN()) 0 else (seaLevel * 10).roundToInt()
        return listOf(temp, high, low, code, seaLevelTenths)
    }

    private fun fail(message: String) {
        prefs.weatherError = message
        WatchState.notifyChanged()
    }

    private fun cachedMessage(): OutMessage? {
        val cache = prefs.weatherCache ?: return null
        if (System.currentTimeMillis() - prefs.weatherCacheTime > CACHE_VALID_MS) return null
        val p = cache.split('|')
        if (p.size < 5) return null
        return Out.weather(p[0].toIntOrNull() ?: 0, p[1].toIntOrNull() ?: 0,
            p[2].toIntOrNull() ?: 0, p[3].toIntOrNull() ?: -1, p[4],
            p.getOrNull(5)?.toIntOrNull() ?: 0)
    }

    private fun manualMessage(): OutMessage =
        Out.weather(prefs.manualTemp, prefs.manualHigh, prefs.manualLow, -1, prefs.manualCondition)

    companion object {
        private const val TAG = "WeatherBridge"
        private const val USER_AGENT = "epaper-watch-companion/1.0"
        private const val STALE_LOCATION_MS = 3L * 60 * 60 * 1000
        private const val CACHE_VALID_MS = 3L * 60 * 60 * 1000

        /** WMO weather interpretation codes (Open-Meteo), at most 20 characters. */
        fun conditionFor(code: Int): String = when (code) {
            0 -> "Clear"
            1 -> "Mostly clear"
            2 -> "Partly cloudy"
            3 -> "Overcast"
            45, 48 -> "Fog"
            51, 53 -> "Drizzle"
            55 -> "Heavy drizzle"
            56, 57 -> "Freezing drizzle"
            61 -> "Light rain"
            63 -> "Rain"
            65 -> "Heavy rain"
            66, 67 -> "Freezing rain"
            71 -> "Light snow"
            73 -> "Snow"
            75 -> "Heavy snow"
            77 -> "Snow grains"
            80 -> "Light showers"
            81 -> "Showers"
            82 -> "Heavy showers"
            85, 86 -> "Snow showers"
            95 -> "Thunderstorm"
            96, 99 -> "Thunder and hail"
            else -> "Unknown"
        }
    }
}
