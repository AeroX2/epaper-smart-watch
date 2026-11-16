package com.james.epaperwatch.ui

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.Typeface
import android.view.View
import java.time.LocalDate
import java.time.format.DateTimeFormatter
import java.time.format.TextStyle
import java.util.Locale

/** Round "watch face" showing link state and the watch battery. */
class StatusDialView(context: Context) : View(context) {
    var headline = "—"
    var caption = ""
    var connected = false
    /** 0..100, or -1 if unknown. */
    var battery = -1

    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val arc = RectF()

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val cx = width / 2f
        val cy = height / 2f
        val r = minOf(width, height) * 0.44f

        paint.style = Paint.Style.FILL
        paint.color = Ui.CARD
        canvas.drawCircle(cx, cy, r, paint)
        paint.style = Paint.Style.STROKE
        paint.strokeWidth = r * 0.03f
        paint.color = Ui.INK
        canvas.drawCircle(cx, cy, r, paint)

        // Battery ring.
        val ringR = r * 0.88f
        arc.set(cx - ringR, cy - ringR, cx + ringR, cy + ringR)
        paint.strokeWidth = r * 0.06f
        paint.strokeCap = Paint.Cap.ROUND
        paint.color = Ui.LINE
        canvas.drawArc(arc, 135f, 270f, false, paint)
        if (battery >= 0) {
            paint.color = if (connected) Ui.COBALT else Ui.MUTED
            canvas.drawArc(arc, 135f, 270f * battery.coerceIn(0, 100) / 100f, false, paint)
        }

        paint.style = Paint.Style.FILL
        paint.textAlign = Paint.Align.CENTER
        paint.typeface = Typeface.create(Typeface.MONOSPACE, Typeface.BOLD)
        paint.color = Ui.INK
        paint.textSize = r * 0.34f
        canvas.drawText(headline, cx, cy + r * 0.08f, paint)
        paint.typeface = Typeface.MONOSPACE
        paint.textSize = r * 0.11f
        paint.letterSpacing = 0.1f
        paint.color = if (connected) Ui.COBALT else Ui.MUTED
        canvas.drawText(caption.uppercase(Locale.ROOT), cx, cy + r * 0.32f, paint)
        paint.letterSpacing = 0f
    }
}

/** Seven-day step bars, oldest on the left. */
class HistoryChartView(context: Context) : View(context) {
    private var days: List<Pair<String, Int>> = emptyList()
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val bar = RectF()
    private val parser = DateTimeFormatter.ofPattern("yyyyMMdd", Locale.US)

    fun setData(data: List<Pair<String, Int>>) {
        days = data.takeLast(7)
        contentDescription = data.joinToString { "${it.first}: ${it.second} steps" }
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val density = resources.displayMetrics.density
        paint.textAlign = Paint.Align.CENTER
        paint.typeface = Typeface.MONOSPACE
        if (days.isEmpty()) {
            paint.color = Ui.MUTED
            paint.textSize = 12 * density
            canvas.drawText("No history yet", width / 2f, height / 2f, paint)
            return
        }
        val max = days.maxOf { it.second }.coerceAtLeast(1)
        val slot = width / 7f
        val labelH = 18 * density
        val valueH = 14 * density
        val chartH = height - labelH - valueH
        days.forEachIndexed { i, (day, steps) ->
            val x = slot * (i + 7 - days.size)
            val h = chartH * steps / max
            bar.set(x + slot * 0.2f, valueH + chartH - h, x + slot * 0.8f, valueH + chartH)
            paint.color = if (i == days.lastIndex) Ui.COBALT else Ui.INK
            canvas.drawRoundRect(bar, 3 * density, 3 * density, paint)
            paint.color = Ui.MUTED
            paint.textSize = 9 * density
            canvas.drawText(compact(steps), x + slot / 2, bar.top - 3 * density, paint)
            paint.textSize = 11 * density
            canvas.drawText(dayLabel(day), x + slot / 2, height - 4 * density, paint)
        }
    }

    private fun compact(steps: Int) = if (steps >= 10_000) "${steps / 1000}k" else steps.toString()

    private fun dayLabel(day: String): String = try {
        LocalDate.parse(day, parser).dayOfWeek.getDisplayName(TextStyle.SHORT, Locale.getDefault())
    } catch (_: Exception) {
        day.takeLast(2)
    }
}
