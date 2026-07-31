package com.james.epaperwatch

import android.Manifest
import android.annotation.SuppressLint
import android.app.Activity
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Typeface
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.text.InputType
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView

class MainActivity : Activity() {
    private lateinit var statusDial: WatchStatusView
    private lateinit var statusLabel: TextView
    private var statusReceiverRegistered = false

    private val statusReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            val connected = intent.getBooleanExtra(WatchLinkService.EXTRA_CONNECTED, false)
            val message = intent.getStringExtra(WatchLinkService.EXTRA_STATUS) ?: "Idle"
            statusLabel.text = message
            statusDial.connected = connected
            statusDial.invalidate()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.statusBarColor = PAPER
        window.navigationBarColor = PAPER
        @Suppress("DEPRECATION")
        window.decorView.systemUiVisibility = View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR
        setContentView(buildInterface())
        requestRuntimePermissions()
        WatchLinkService.start(this)
    }

    @SuppressLint("UnspecifiedRegisterReceiverFlag")
    override fun onStart() {
        super.onStart()
        val filter = IntentFilter(WatchLinkService.ACTION_STATUS)
        if (Build.VERSION.SDK_INT >= 33) {
            registerReceiver(statusReceiver, filter, RECEIVER_NOT_EXPORTED)
        } else {
            @Suppress("DEPRECATION")
            registerReceiver(statusReceiver, filter)
        }
        statusReceiverRegistered = true
    }

    override fun onStop() {
        if (statusReceiverRegistered) {
            unregisterReceiver(statusReceiver)
            statusReceiverRegistered = false
        }
        super.onStop()
    }

    private fun requestRuntimePermissions() {
        val permissions = mutableListOf<String>()
        if (Build.VERSION.SDK_INT >= 31) {
            permissions += Manifest.permission.BLUETOOTH_SCAN
            permissions += Manifest.permission.BLUETOOTH_CONNECT
        } else {
            permissions += Manifest.permission.ACCESS_COARSE_LOCATION
            permissions += Manifest.permission.ACCESS_FINE_LOCATION
        }
        if (Build.VERSION.SDK_INT >= 33) {
            permissions += Manifest.permission.POST_NOTIFICATIONS
        }
        val missing = permissions.filter {
            checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED
        }
        if (missing.isNotEmpty()) requestPermissions(missing.toTypedArray(), 41)
    }

    private fun buildInterface(): View {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(22), dp(25), dp(22), dp(42))
            setBackgroundColor(PAPER)
        }
        root.addView(label("FIELD CONSOLE / WB5MMG", 11, COBALT).apply {
            letterSpacing = 0.16f
            typeface = Typeface.MONOSPACE
        })
        root.addView(label("E-Paper Watch", 34, INK).apply {
            setTypeface(Typeface.create("sans-serif-condensed", Typeface.BOLD))
            setPadding(0, dp(5), 0, dp(20))
        })

        statusDial = WatchStatusView(this)
        root.addView(statusDial, LinearLayout.LayoutParams(dp(226), dp(226)).apply {
            gravity = Gravity.CENTER_HORIZONTAL
        })
        statusLabel = label("Idle", 13, INK).apply {
            gravity = Gravity.CENTER
            setPadding(0, dp(12), 0, dp(20))
            typeface = Typeface.MONOSPACE
        }
        root.addView(statusLabel)

        root.addView(actionRow(
            actionButton("Connect") {
                requestRuntimePermissions()
                WatchLinkService.connect(this)
            },
            actionButton("Disconnect", primary = false) {
                WatchLinkService.disconnect(this)
            },
        ))

        root.addView(section("PHONE LINK", "Time, notifications and media"))
        root.addView(actionButton("Sync phone time") {
            WatchLinkService.send(this, WatchProtocol.time())
        })
        root.addView(actionButton("Enable notification access", primary = false) {
            startActivity(Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS))
        })
        root.addView(actionButton("Send a test notification", primary = false) {
            WatchLinkService.send(
                this,
                WatchProtocol.notification(
                    "COMPANION",
                    "RADIO LINK ONLINE",
                    "Notification relay is working.",
                ),
            )
        })

        root.addView(section("WEATHER CARD", "Manual values for hardware testing"))
        val current = numericField("18")
        val high = numericField("22")
        val low = numericField("12")
        val condition = textField("PARTLY CLOUDY")
        root.addView(fieldRow(field("CURRENT °C", current), field("HIGH °C", high)))
        root.addView(fieldRow(field("LOW °C", low), field("CONDITION", condition)))
        root.addView(actionButton("Send weather to watch") {
            WatchLinkService.send(
                this,
                WatchProtocol.weather(
                    current.text.toString().toIntOrNull() ?: 18,
                    high.text.toString().toIntOrNull() ?: 22,
                    low.text.toString().toIntOrNull() ?: 12,
                    condition.text,
                ),
            )
        })

        root.addView(section("SLEEP AS ANDROID", "Movement-only wearable bridge"))
        root.addView(label(
            "The bridge listens for Sleep’s tracking, pause, notification and alarm intents. " +
                "These two controls exercise the watch sampler without starting a real session.",
            12,
            MUTED,
        ).apply { setPadding(0, 0, 0, dp(10)) })
        root.addView(actionRow(
            actionButton("Test tracking") {
                WatchLinkService.send(this, "SLEEP,START")
            },
            actionButton("Stop tracking", primary = false) {
                WatchLinkService.send(this, "SLEEP,STOP")
            },
        ))
        root.addView(label(
            "Before connecting: flash the firmware, open USB serial, press r, and wait for " +
                "“BLE advertising”. Sleep integration also needs com.james.epaperwatch " +
                "whitelisted by Urbandroid.",
            11,
            MUTED,
        ).apply {
            setPadding(0, dp(24), 0, 0)
            typeface = Typeface.MONOSPACE
        })

        return ScrollView(this).apply {
            isFillViewport = true
            addView(root, ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT,
            ))
        }
    }

    private fun section(title: String, subtitle: String): View =
        LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(0, dp(30), 0, dp(10))
            addView(label(title, 12, COBALT).apply {
                typeface = Typeface.MONOSPACE
                letterSpacing = 0.12f
            })
            addView(label(subtitle, 19, INK).apply {
                setTypeface(Typeface.create("sans-serif-condensed", Typeface.BOLD))
            })
        }

    private fun actionButton(
        text: String,
        primary: Boolean = true,
        action: () -> Unit,
    ): Button = Button(this).apply {
        this.text = text
        isAllCaps = false
        textSize = 13f
        setTextColor(if (primary) Color.WHITE else INK)
        setBackgroundColor(if (primary) COBALT else Color.TRANSPARENT)
        stateListAnimator = null
        setPadding(dp(13), dp(4), dp(13), dp(4))
        setOnClickListener { action() }
        layoutParams = LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            dp(52),
        ).apply {
            setMargins(0, dp(5), 0, dp(5))
        }
    }

    private fun actionRow(left: View, right: View): View =
        LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            addView(left, LinearLayout.LayoutParams(0, dp(58), 1f).apply {
                marginEnd = dp(5)
            })
            addView(right, LinearLayout.LayoutParams(0, dp(58), 1f).apply {
                marginStart = dp(5)
            })
        }

    private fun field(label: String, input: EditText): View =
        LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            addView(label(label, 10, MUTED).apply {
                typeface = Typeface.MONOSPACE
            })
            addView(input)
        }

    private fun fieldRow(left: View, right: View): View =
        LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            addView(left, LinearLayout.LayoutParams(0, dp(68), 1f).apply {
                marginEnd = dp(6)
            })
            addView(right, LinearLayout.LayoutParams(0, dp(68), 1f).apply {
                marginStart = dp(6)
            })
        }

    private fun numericField(value: String): EditText = textField(value).apply {
        inputType = InputType.TYPE_CLASS_NUMBER or InputType.TYPE_NUMBER_FLAG_SIGNED
    }

    private fun textField(value: String): EditText = EditText(this).apply {
        setText(value)
        setTextColor(INK)
        textSize = 14f
        setSingleLine(true)
        setPadding(dp(8), 0, dp(8), 0)
    }

    private fun label(value: String, size: Int, color: Int): TextView =
        TextView(this).apply {
            text = value
            textSize = size.toFloat()
            setTextColor(color)
            includeFontPadding = false
        }

    private fun dp(value: Int): Int =
        (value * resources.displayMetrics.density + 0.5f).toInt()

    companion object {
        private val PAPER = Color.rgb(238, 240, 234)
        private val INK = Color.rgb(23, 26, 24)
        private val MUTED = Color.rgb(91, 101, 95)
        private val COBALT = Color.rgb(36, 71, 216)
    }
}

private class WatchStatusView(context: Context) : View(context) {
    var connected = false
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val center = width / 2f
        val radius = minOf(width, height) * 0.46f
        paint.style = Paint.Style.FILL
        paint.color = Color.rgb(255, 255, 252)
        canvas.drawCircle(center, height / 2f, radius, paint)
        paint.style = Paint.Style.STROKE
        paint.strokeWidth = 5f
        paint.color = Color.rgb(23, 26, 24)
        canvas.drawCircle(center, height / 2f, radius, paint)

        paint.style = Paint.Style.FILL
        paint.typeface = Typeface.create(Typeface.MONOSPACE, Typeface.BOLD)
        paint.textAlign = Paint.Align.CENTER
        paint.color = Color.rgb(23, 26, 24)
        paint.textSize = radius * 0.39f
        canvas.drawText(if (connected) "LINK" else "WAIT", center, height * 0.48f, paint)
        paint.textSize = radius * 0.12f
        paint.letterSpacing = 0.12f
        canvas.drawText(
            if (connected) "WATCH CONNECTED" else "PRESS r ON WATCH",
            center,
            height * 0.61f,
            paint,
        )
        paint.letterSpacing = 0f

        val y = height * 0.70f
        repeat(5) { index ->
            paint.style = if (connected || index == 0) Paint.Style.FILL else Paint.Style.STROKE
            canvas.drawRect(center - 30f + index * 15f, y, center - 22f + index * 15f, y + 8f, paint)
        }
    }
}
