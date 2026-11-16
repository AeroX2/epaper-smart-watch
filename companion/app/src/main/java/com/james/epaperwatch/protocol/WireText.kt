package com.james.epaperwatch.protocol

import java.text.Normalizer

/**
 * Text-field sanitiser from PROTOCOL.md "Framing":
 * NFD-normalise, drop combining marks, `|` -> `/`, newline/tab -> space, drop anything else
 * outside printable ASCII, collapse runs of spaces, trim, cut to the field's maximum length.
 *
 * The result is pure ASCII, so its length in characters equals its length in bytes.
 */
object WireText {
    fun sanitize(value: CharSequence?, max: Int): String {
        if (value.isNullOrEmpty() || max <= 0) return ""
        val decomposed = Normalizer.normalize(value, Normalizer.Form.NFD)
        val out = StringBuilder(decomposed.length)
        var lastWasSpace = false
        for (ch in decomposed) {
            val mapped: Char = when {
                isCombiningMark(ch) -> continue
                ch == '|' -> '/'
                ch == '\n' || ch == '\r' || ch == '\t' -> ' '
                ch in ' '..'~' -> ch
                else -> continue
            }
            if (mapped == ' ') {
                if (lastWasSpace) continue
                lastWasSpace = true
            } else {
                lastWasSpace = false
            }
            out.append(mapped)
        }
        return out.toString().trim().take(max).trimEnd()
    }

    private fun isCombiningMark(ch: Char): Boolean = when (Character.getType(ch)) {
        Character.NON_SPACING_MARK.toInt(),
        Character.COMBINING_SPACING_MARK.toInt(),
        Character.ENCLOSING_MARK.toInt() -> true
        else -> false
    }
}
