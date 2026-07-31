package com.james.epaperwatch

import android.annotation.SuppressLint
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Build
import kotlin.math.max

class SleepApiBridge(
    private val context: Context,
    private val isWatchConnected: () -> Boolean,
    private val sendToWatch: (String) -> Unit,
) : BroadcastReceiver() {
    private val movementBatch = ArrayList<Float>()
    private var requestedBatchSize = 12
    private var registered = false

    @SuppressLint("UnspecifiedRegisterReceiverFlag")
    fun register() {
        if (registered) return
        val filter = IntentFilter().apply {
            addAction(CHECK_CONNECTED)
            addAction(START_TRACKING)
            addAction(STOP_TRACKING)
            addAction(SET_PAUSE)
            addAction(SET_SUSPENDED)
            addAction(SET_BATCH_SIZE)
            addAction(START_ALARM)
            addAction(STOP_ALARM)
            addAction(UPDATE_ALARM)
            addAction(SHOW_NOTIFICATION)
            addAction(HINT)
        }
        if (Build.VERSION.SDK_INT >= 33) {
            context.registerReceiver(this, filter, Context.RECEIVER_EXPORTED)
        } else {
            @Suppress("DEPRECATION")
            context.registerReceiver(this, filter)
        }
        registered = true
    }

    fun unregister() {
        if (!registered) return
        context.unregisterReceiver(this)
        registered = false
    }

    override fun onReceive(receiverContext: Context, intent: Intent) {
        when (intent.action) {
            CHECK_CONNECTED -> if (isWatchConnected()) {
                sendToSleep(Intent(CONFIRM_CONNECTED))
            }
            START_TRACKING -> {
                movementBatch.clear()
                sendToWatch("SLEEP,START")
            }
            STOP_TRACKING -> {
                flushMovement()
                sendToWatch("SLEEP,STOP")
            }
            SET_PAUSE -> {
                val untilSeconds = max(0L, intent.getLongExtra("TIMESTAMP", 0L) / 1000L)
                sendToWatch("SLEEP,PAUSE,$untilSeconds")
            }
            SET_SUSPENDED -> {
                val suspended = intent.getBooleanExtra("SUSPENDED", false)
                val until = if (suspended) 4_102_444_800L else 0L
                sendToWatch("SLEEP,PAUSE,$until")
            }
            SET_BATCH_SIZE -> {
                requestedBatchSize =
                    intent.getLongExtra("SIZE", 12L).toInt().coerceIn(1, 120)
                if (movementBatch.size >= requestedBatchSize) flushMovement()
            }
            START_ALARM -> {
                val delay = intent.getIntExtra("DELAY", 0)
                val wireDelay = if (delay < 0) 4_294_967_295L else delay.toLong()
                sendToWatch("SLEEP,ALARM,$wireDelay")
            }
            STOP_ALARM -> sendToWatch("SLEEP,ALARM_STOP")
            UPDATE_ALARM -> {
                val timestamp = intent.getLongExtra("TIMESTAMP", 0L)
                if (timestamp > 0L) {
                    val delay = (timestamp - System.currentTimeMillis()).coerceAtLeast(0L)
                    sendToWatch("SLEEP,ALARM,${delay.coerceAtMost(4_294_967_294L)}")
                }
            }
            SHOW_NOTIFICATION -> {
                val title = WatchProtocol.clean(intent.getStringExtra("TITLE"), 28)
                val text = WatchProtocol.clean(intent.getStringExtra("TEXT"), 28)
                sendToWatch("SLEEP,NOTIFY,$title|$text")
            }
            HINT -> {
                val repeats = intent.getIntExtra("REPEAT", 1).coerceIn(1, 5)
                sendToWatch("SLEEP,HINT,$repeats")
            }
        }
    }

    fun onMovement(maxDeltaMg: Int) {
        // Sleep expects acceleration change in m/s². The watch sends milligravity.
        movementBatch += maxDeltaMg.coerceAtLeast(0) * 0.00980665f
        if (movementBatch.size >= requestedBatchSize) flushMovement()
    }

    fun onWatchCommand(command: String) {
        when (command) {
            "SLEEP,SNOOZE" -> sendToSleep(Intent(SNOOZE_FROM_WATCH))
            "SLEEP,DISMISS" -> sendToSleep(Intent(DISMISS_FROM_WATCH))
            "SLEEP,PAUSE" -> sendToSleep(Intent(PAUSE_FROM_WATCH))
            "SLEEP,RESUME" -> sendToSleep(Intent(RESUME_FROM_WATCH))
        }
    }

    private fun flushMovement() {
        if (movementBatch.isEmpty()) return
        sendToSleep(Intent(DATA_UPDATE).putExtra("MAX_RAW_DATA", movementBatch.toFloatArray()))
        movementBatch.clear()
    }

    private fun sendToSleep(intent: Intent) {
        intent.setPackage(SLEEP_PACKAGE)
        context.sendBroadcast(intent)
    }

    companion object {
        private const val SLEEP_PACKAGE = "com.urbandroid.sleep"
        private const val PREFIX = "com.urbandroid.sleep.watch."

        const val CHECK_CONNECTED = "${PREFIX}CHECK_CONNECTED"
        const val CONFIRM_CONNECTED = "${PREFIX}CONFIRM_CONNECTED"
        const val START_TRACKING = "${PREFIX}START_TRACKING"
        const val STOP_TRACKING = "${PREFIX}STOP_TRACKING"
        const val SET_PAUSE = "${PREFIX}SET_PAUSE"
        const val SET_SUSPENDED = "${PREFIX}SET_SUSPENDED"
        const val SET_BATCH_SIZE = "${PREFIX}SET_BATCH_SIZE"
        const val START_ALARM = "${PREFIX}START_ALARM"
        const val STOP_ALARM = "${PREFIX}STOP_ALARM"
        const val UPDATE_ALARM = "${PREFIX}UPDATE_ALARM"
        const val SHOW_NOTIFICATION = "${PREFIX}SHOW_NOTIFICATION"
        const val HINT = "${PREFIX}HINT"
        const val DATA_UPDATE = "${PREFIX}DATA_UPDATE"
        const val PAUSE_FROM_WATCH = "${PREFIX}PAUSE_FROM_WATCH"
        const val RESUME_FROM_WATCH = "${PREFIX}RESUME_FROM_WATCH"
        const val SNOOZE_FROM_WATCH = "${PREFIX}SNOOZE_FROM_WATCH"
        const val DISMISS_FROM_WATCH = "${PREFIX}DISMISS_FROM_WATCH"
    }
}
