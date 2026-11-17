package com.james.epaperwatch.ui

import android.content.Context
import android.content.res.ColorStateList
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.RippleDrawable
import android.text.InputType
import android.view.Gravity
import android.view.HapticFeedbackConstants
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.Switch
import android.widget.TextView

/** Tiny programmatic view kit in the app's paper / ink / cobalt style. */
class Ui(private val context: Context) {
    fun dp(value: Int): Int = (value * context.resources.displayMetrics.density + 0.5f).toInt()

    fun column(): LinearLayout = LinearLayout(context).apply { orientation = LinearLayout.VERTICAL }

    fun label(value: CharSequence, size: Int, color: Int = INK, mono: Boolean = false): TextView =
        TextView(context).apply {
            text = value
            textSize = size.toFloat()
            setTextColor(color)
            includeFontPadding = false
            if (mono) typeface = Typeface.MONOSPACE
        }

    fun eyebrow(value: String): TextView = label(value, 11, COBALT, mono = true).apply {
        letterSpacing = 0.14f
    }

    fun section(title: String, subtitle: String): View = column().apply {
        setPadding(0, dp(30), 0, dp(10))
        addView(eyebrow(title))
        addView(label(subtitle, 19).apply {
            setTypeface(Typeface.create("sans-serif-condensed", Typeface.BOLD))
            setPadding(0, dp(3), 0, 0)
        })
    }

    fun note(value: CharSequence): TextView = label(value, 12, MUTED).apply {
        setPadding(0, dp(2), 0, dp(8))
        setLineSpacing(0f, 1.15f)
    }

    fun button(text: String, primary: Boolean = true, action: () -> Unit): Button =
        actionButton(text, primary) { action() }

    /**
     * A button whose action gets the button itself, so it can show that it is working (see
     * [setBusy]). Every button ripples and gives a haptic tick when pressed, so a press is
     * never silent.
     */
    fun actionButton(text: String, primary: Boolean = true, action: (Button) -> Unit): Button =
        Button(context).apply {
            this.text = text
            isAllCaps = false
            textSize = 14f
            setTextColor(if (primary) Color.WHITE else INK)
            val fill = GradientDrawable().apply {
                setColor(if (primary) COBALT else LINE)
                cornerRadius = dp(6).toFloat()
            }
            val ripple = if (primary) Color.argb(110, 255, 255, 255) else Color.argb(70, 23, 26, 24)
            background = RippleDrawable(ColorStateList.valueOf(ripple), fill, null)
            stateListAnimator = null
            setPadding(dp(12), dp(4), dp(12), dp(4))
            setOnClickListener {
                it.performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY)
                action(this)
            }
            layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(50))
                .apply { setMargins(0, dp(4), 0, dp(4)) }
        }

    /** Shows [working] on a disabled, dimmed button; `null` restores [label] and enables it. */
    fun setBusy(button: Button, label: CharSequence, working: CharSequence?) {
        button.isEnabled = working == null
        button.alpha = if (working == null) 1f else 0.55f
        button.text = working ?: label
    }

    fun row(vararg views: View): LinearLayout = LinearLayout(context).apply {
        orientation = LinearLayout.HORIZONTAL
        views.forEachIndexed { index, view ->
            addView(view, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f).apply {
                if (index > 0) marginStart = dp(8)
            })
        }
    }

    fun switchRow(title: String, subtitle: String? = null, onChange: (Switch, Boolean) -> Unit): Switch =
        Switch(context).apply {
            text = if (subtitle == null) title else "$title\n$subtitle"
            textSize = 15f
            setTextColor(INK)
            setPadding(0, dp(10), 0, dp(10))
            setOnCheckedChangeListener { view, checked -> onChange(view as Switch, checked) }
        }

    fun field(label: String, input: EditText): View = column().apply {
        addView(label(label, 10, MUTED, mono = true))
        addView(input)
    }

    fun textInput(value: String, numeric: Boolean = false, decimal: Boolean = false): EditText =
        EditText(context).apply {
            setText(value)
            setTextColor(INK)
            textSize = 15f
            setSingleLine(true)
            if (numeric) {
                inputType = InputType.TYPE_CLASS_NUMBER or InputType.TYPE_NUMBER_FLAG_SIGNED or
                    (if (decimal) InputType.TYPE_NUMBER_FLAG_DECIMAL else 0)
            }
        }

    /** A two-column "label  value" line. Returns the value view. */
    fun keyValue(parent: LinearLayout, key: String): TextView {
        val value = label("—", 14, INK, mono = true).apply { gravity = Gravity.END }
        val line = LinearLayout(context).apply {
            orientation = LinearLayout.HORIZONTAL
            setPadding(0, dp(5), 0, dp(5))
            addView(label(key, 13, MUTED), LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
            addView(value)
        }
        parent.addView(line)
        return value
    }

    companion object {
        val PAPER = Color.rgb(238, 240, 234)
        val CARD = Color.rgb(255, 255, 252)
        val INK = Color.rgb(23, 26, 24)
        val MUTED = Color.rgb(91, 101, 95)
        val LINE = Color.rgb(222, 225, 216)
        val COBALT = Color.rgb(36, 71, 216)
        val ALERT = Color.rgb(196, 64, 32)
    }
}
