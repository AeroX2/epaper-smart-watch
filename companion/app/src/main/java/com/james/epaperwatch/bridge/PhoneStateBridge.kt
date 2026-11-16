package com.james.epaperwatch.bridge

import android.app.AlarmManager
import android.app.NotificationManager
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.BatteryManager
import android.os.Build
import com.james.epaperwatch.protocol.Out
import com.james.epaperwatch.protocol.OutMessage
import java.util.TimeZone

/**
 * TIME, PHONE (battery / charging / DND) and NEXT_ALARM sources. Fires [onChange] with the
 * kind of data that changed; the service de-duplicates (and, for the battery level, coarsens)
 * before sending.
 */
class PhoneStateBridge(
    private val context: Context,
    private val onChange: (Kind) -> Unit,
) : BroadcastReceiver() {
    enum class Kind { TIME, PHONE, ALARM }

    private var battery = 0
    private var charging = false
    private var registered = false

    fun register() {
        if (registered) return
        val filter = IntentFilter().apply {
            addAction(Intent.ACTION_TIME_CHANGED)
            addAction(Intent.ACTION_TIMEZONE_CHANGED)
            addAction(Intent.ACTION_DATE_CHANGED)
            addAction(AlarmManager.ACTION_NEXT_ALARM_CLOCK_CHANGED)
            addAction(Intent.ACTION_BATTERY_CHANGED)
            addAction(NotificationManager.ACTION_INTERRUPTION_FILTER_CHANGED)
        }
        val sticky = if (Build.VERSION.SDK_INT >= 33) {
            context.registerReceiver(this, filter, Context.RECEIVER_NOT_EXPORTED)
        } else {
            @Suppress("UnspecifiedRegisterReceiverFlag")
            context.registerReceiver(this, filter)
        }
        sticky?.let(::readBattery)
        registered = true
    }

    fun unregister() {
        if (!registered) return
        context.unregisterReceiver(this)
        registered = false
    }

    override fun onReceive(receiverContext: Context, intent: Intent) {
        when (intent.action) {
            Intent.ACTION_BATTERY_CHANGED -> {
                // Sent for every voltage or temperature change too: only a new level or
                // plug state is passed on (the service then sends it in coarse steps).
                val levelBefore = battery
                val chargingBefore = charging
                readBattery(intent)
                if (battery != levelBefore || charging != chargingBefore) onChange(Kind.PHONE)
            }
            NotificationManager.ACTION_INTERRUPTION_FILTER_CHANGED -> onChange(Kind.PHONE)
            AlarmManager.ACTION_NEXT_ALARM_CLOCK_CHANGED -> onChange(Kind.ALARM)
            Intent.ACTION_TIMEZONE_CHANGED -> {
                TimeZone.setDefault(null) // make sure the cached default zone is refreshed
                onChange(Kind.TIME)
            }
            Intent.ACTION_TIME_CHANGED, Intent.ACTION_DATE_CHANGED -> onChange(Kind.TIME)
        }
    }

    private fun readBattery(intent: Intent) {
        val level = intent.getIntExtra(BatteryManager.EXTRA_LEVEL, -1)
        val scale = intent.getIntExtra(BatteryManager.EXTRA_SCALE, 100)
        if (level >= 0 && scale > 0) battery = level * 100 / scale
        charging = intent.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0) != 0
    }

    /**
     * TIME for "now". The reliable channel rebuilds it at every transmission attempt (see
     * [OutMessage.refresh]), so a queued or retried TIME is stamped when it goes on the air.
     */
    fun timeMessage(): OutMessage = buildTime(refreshing = true)

    private fun buildTime(refreshing: Boolean): OutMessage {
        val now = System.currentTimeMillis()
        val offset = TimeZone.getDefault().getOffset(now) / 1000
        // Tell the watch about the next daylight-saving change so it can switch on time
        // by itself: the phone gets no broadcast for it and may be asleep or out of range.
        val next = try {
            java.time.ZoneId.systemDefault().rules
                .nextTransition(java.time.Instant.ofEpochMilli(now))
        } catch (_: RuntimeException) {
            null
        }
        return Out.time(
            now / 1000L,
            offset,
            next?.instant?.epochSecond ?: 0L,
            next?.offsetAfter?.totalSeconds ?: 0,
            if (refreshing) { { buildTime(refreshing = false) } } else null,
        )
    }

    fun phoneMessage(): OutMessage = Out.phone(battery, charging, isDnd())

    fun alarmMessage(): OutMessage {
        val next = context.getSystemService(AlarmManager::class.java)?.nextAlarmClock
        return Out.nextAlarm((next?.triggerTime ?: 0L) / 1000L)
    }

    private fun isDnd(): Boolean {
        val filter = context.getSystemService(NotificationManager::class.java)
            ?.currentInterruptionFilter ?: return false
        return filter != NotificationManager.INTERRUPTION_FILTER_ALL &&
            filter != NotificationManager.INTERRUPTION_FILTER_UNKNOWN
    }
}
