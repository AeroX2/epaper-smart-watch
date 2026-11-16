package com.james.epaperwatch.ui

import android.app.Activity
import android.graphics.Typeface
import android.os.Bundle
import android.view.ViewGroup
import android.widget.ScrollView

/**
 * Shown by Health Connect ("privacy policy" link on its permission screen, and the app's entry
 * in the system's health permission usage): what this app does with health data.
 */
class HealthRationaleActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val ui = Ui(this)
        window.statusBarColor = Ui.PAPER
        window.navigationBarColor = Ui.PAPER
        val root = ui.column().apply {
            setPadding(ui.dp(22), ui.dp(24), ui.dp(22), ui.dp(40))
            setBackgroundColor(Ui.PAPER)
        }
        root.addView(ui.eyebrow("HEALTH CONNECT"))
        root.addView(ui.label("Step data", 30).apply {
            setTypeface(Typeface.create("sans-serif-condensed", Typeface.BOLD))
            setPadding(0, ui.dp(4), 0, ui.dp(12))
        })
        root.addView(ui.label(
            "When \"Sync steps to Health Connect\" is switched on, this app writes the step " +
                "counts measured by your e-paper watch to Health Connect: one record per day.\n\n" +
                "It only writes steps. It never reads any health data, and it sends nothing " +
                "to any server: the step counts travel from the watch to this phone over " +
                "Bluetooth and from this app into Health Connect on the phone.\n\n" +
                "Switch the option off in this app, or remove the permission in Health " +
                "Connect, to stop. Records already written can be deleted in Health Connect.",
            15,
        ).apply { setLineSpacing(0f, 1.2f) })
        setContentView(ScrollView(this).apply {
            setBackgroundColor(Ui.PAPER)
            addView(root, ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        })
    }
}
