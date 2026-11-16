package com.james.epaperwatch.ui

import android.app.Activity
import android.graphics.Typeface
import android.os.Bundle
import android.view.Gravity
import android.view.ViewGroup
import android.widget.CheckBox
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.Switch
import com.james.epaperwatch.WatchNotificationListener
import com.james.epaperwatch.data.Prefs

/**
 * Per-app notification filter: every app that has posted a forwardable notification, with a
 * "forward" switch and a "hide text" (private) checkbox.
 */
class AppFilterActivity : Activity() {
    private lateinit var ui: Ui
    private lateinit var prefs: Prefs
    private var changed = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        ui = Ui(this)
        prefs = Prefs(this)
        window.statusBarColor = Ui.PAPER
        window.navigationBarColor = Ui.PAPER

        val root = ui.column().apply {
            setPadding(ui.dp(22), ui.dp(24), ui.dp(22), ui.dp(40))
            setBackgroundColor(Ui.PAPER)
        }
        root.addView(ui.eyebrow("NOTIFICATIONS"))
        root.addView(ui.label("Apps", 30).apply {
            setTypeface(Typeface.create("sans-serif-condensed", Typeface.BOLD))
            setPadding(0, ui.dp(4), 0, ui.dp(8))
        })
        root.addView(ui.note(
            "Apps appear here after they post a notification. Switch an app off to stop " +
                "forwarding it. \"Hide text\" sends only the app and title, never the message.",
        ))

        val apps = prefs.knownApps.entries.sortedBy { it.value.lowercase() }
        if (apps.isEmpty()) {
            root.addView(ui.label("No apps yet. Grant notification access and wait for a notification.", 14, Ui.MUTED).apply {
                setPadding(0, ui.dp(24), 0, 0)
            })
        }
        val denied = prefs.deniedApps
        val private = prefs.privateApps
        for ((pkg, label) in apps) root.addView(appRow(pkg, label, pkg !in denied, pkg in private))

        setContentView(ScrollView(this).apply {
            setBackgroundColor(Ui.PAPER)
            addView(root, ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        })
    }

    override fun onPause() {
        super.onPause()
        if (changed) {
            WatchNotificationListener.reevaluate()
            changed = false
        }
    }

    private fun appRow(pkg: String, label: String, forwarded: Boolean, hidden: Boolean): LinearLayout {
        val names = ui.column().apply {
            addView(ui.label(label, 15))
            addView(ui.label(pkg, 11, Ui.MUTED, mono = true).apply { setPadding(0, ui.dp(2), 0, 0) })
        }
        val hide = CheckBox(this).apply {
            text = "Hide text"
            textSize = 12f
            setTextColor(Ui.MUTED)
            isChecked = hidden
            isEnabled = forwarded
            setOnCheckedChangeListener { _, on ->
                prefs.setPrivate(pkg, on)
                changed = true
            }
        }
        val forward = Switch(this).apply {
            isChecked = forwarded
            contentDescription = "Forward $label"
            setOnCheckedChangeListener { _, on ->
                prefs.setDenied(pkg, !on)
                hide.isEnabled = on
                changed = true
            }
        }
        return LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(0, ui.dp(10), 0, ui.dp(10))
            addView(names, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
            addView(hide)
            addView(forward)
        }
    }
}
