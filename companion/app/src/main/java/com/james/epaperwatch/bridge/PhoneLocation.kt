package com.james.epaperwatch.bridge

import android.Manifest
import android.annotation.SuppressLint
import android.content.Context
import android.content.pm.PackageManager
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.util.Log
import com.james.epaperwatch.data.Prefs
import java.util.Locale

/**
 * The phone's approximate position for the weather forecast.
 *
 * On Android 10+ a foreground service of type connectedDevice counts as *background* for
 * location: without `ACCESS_BACKGROUND_LOCATION` it gets no fix while the app's UI is closed.
 * So every fix that is obtained is cached (rounded to ~1 km) in prefs, the UI refreshes that
 * cache whenever it is opened, and the service falls back to the cached fix.
 */
object PhoneLocation {
    class Fix(val latitude: Double, val longitude: Double, val timeMs: Long)

    private const val TAG = "PhoneLocation"
    private const val SINGLE_FIX_TIMEOUT_MS = 20_000L

    fun hasPermission(context: Context): Boolean =
        context.checkSelfPermission(Manifest.permission.ACCESS_COARSE_LOCATION) ==
            PackageManager.PERMISSION_GRANTED

    /** Before Android 10 there is no separate background permission. */
    fun hasBackgroundPermission(context: Context): Boolean =
        Build.VERSION.SDK_INT < 29 ||
            context.checkSelfPermission(Manifest.permission.ACCESS_BACKGROUND_LOCATION) ==
            PackageManager.PERMISSION_GRANTED

    fun cached(context: Context): Fix? {
        val parts = Prefs(context).weatherFix?.split('|') ?: return null
        if (parts.size != 3) return null
        return Fix(
            parts[0].toDoubleOrNull() ?: return null,
            parts[1].toDoubleOrNull() ?: return null,
            parts[2].toLongOrNull() ?: return null,
        )
    }

    /**
     * Delivers (on [handler]'s thread, exactly once) the newest known fix: the last known or
     * cached one if it is younger than [maxAgeMs], otherwise one new coarse network fix,
     * falling back to whatever older fix exists. Null if there is none at all.
     */
    @SuppressLint("MissingPermission")
    fun obtain(context: Context, handler: Handler, maxAgeMs: Long, callback: (Fix?) -> Unit) {
        val app = context.applicationContext
        val lm = app.getSystemService(LocationManager::class.java)
        if (lm == null || !hasPermission(app)) {
            handler.post { callback(cached(app)) }
            return
        }
        val lastKnown: Location? = try {
            lm.getProviders(true).mapNotNull {
                try {
                    lm.getLastKnownLocation(it)
                } catch (e: SecurityException) {
                    null // e.g. the GPS provider with only coarse permission (API <= 30)
                }
            }.maxByOrNull { it.time }
        } catch (e: SecurityException) {
            null
        }
        val known = newest(lastKnown?.let(::toFix), cached(app))
        if (known != null && System.currentTimeMillis() - known.timeMs < maxAgeMs) {
            handler.post { callback(store(app, known)) }
            return
        }

        var delivered = false
        fun deliver(location: Location?) {
            if (delivered) return
            delivered = true
            val best = newest(location?.let(::toFix), known)
            callback(best?.let { store(app, it) })
        }
        val networkEnabled = try {
            lm.isProviderEnabled(LocationManager.NETWORK_PROVIDER)
        } catch (e: RuntimeException) {
            false
        }
        if (!networkEnabled) {
            handler.post { deliver(null) }
            return
        }
        try {
            if (Build.VERSION.SDK_INT >= 30) {
                lm.getCurrentLocation(LocationManager.NETWORK_PROVIDER, null, { handler.post(it) }) {
                    deliver(it)
                }
            } else {
                val listener = object : LocationListener {
                    override fun onLocationChanged(location: Location) = deliver(location)

                    // Abstract on the platform versions this branch runs on.
                    @Deprecated("Required before API 29")
                    override fun onStatusChanged(provider: String?, status: Int, extras: Bundle?) = Unit
                    override fun onProviderEnabled(provider: String) = Unit
                    override fun onProviderDisabled(provider: String) = Unit
                }
                @Suppress("DEPRECATION")
                lm.requestSingleUpdate(LocationManager.NETWORK_PROVIDER, listener, handler.looper)
                handler.postDelayed({
                    lm.removeUpdates(listener)
                    deliver(null)
                }, SINGLE_FIX_TIMEOUT_MS)
            }
        } catch (e: RuntimeException) {
            Log.w(TAG, "Could not request a location fix", e)
            handler.post { deliver(null) }
        }
    }

    private fun newest(a: Fix?, b: Fix?): Fix? = when {
        a == null -> b
        b == null -> a
        a.timeMs >= b.timeMs -> a
        else -> b
    }

    private fun toFix(location: Location) = Fix(location.latitude, location.longitude, location.time)

    /** Two decimals (~1 km): plenty for a forecast, and no precise position is kept. */
    private fun store(context: Context, fix: Fix): Fix {
        Prefs(context).weatherFix = String.format(
            Locale.US, "%.2f|%.2f|%d", fix.latitude, fix.longitude, fix.timeMs,
        )
        return fix
    }
}
