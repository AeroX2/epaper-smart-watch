package com.james.epaperwatch.bridge

/**
 * Hides likely one-time codes in notification text.
 *
 * A standalone 4-8 digit group (also written "123 456" / "123-456") is masked only when a
 * code keyword (code, OTP, verification, PIN, ...) is close to it: in the same sentence and
 * at most [MAX_DISTANCE] characters away. A group is never masked when it is clearly
 * something else:
 *  - money: a currency symbol or code next to it, or a decimal part;
 *  - a year 1900-2099 next to a month name or inside a date;
 *  - part of a longer, phone-number-shaped run of digit groups.
 *
 * Service-prefixed codes such as "G-123456" are masked even without a keyword.
 *
 * Pure JVM code (unit tested in `OtpRedactorTest`).
 */
object OtpRedactor {
    /** "~40 characters"; a little slack for "OTP for transaction of Rs. 500 at SHOP is 123456". */
    const val MAX_DISTANCE = 48

    private val KEYWORDS = Regex(
        "\\b(?:code|otp|one[- ]?time|verification|verify|passcode|pass code|password|pin|2fa|" +
            "authentication|authenticate|auth|log[- ]?in|sign[- ]?in|token)\\b",
        RegexOption.IGNORE_CASE,
    )
    private val CANDIDATE = Regex("(?<!\\d)(?:\\d{3}[- ]\\d{3}|\\d{4,8})(?!\\d)")
    private val PREFIXED = Regex("(?<![A-Za-z0-9])[A-Z]{1,2}-(\\d{5,8})(?![\\d-])")

    /** Digit groups joined by at most two separators; a '.' only counts inside a number. */
    private val DIGIT_RUN = Regex("\\+?\\(?\\d(?:(?:\\.(?=\\d)|[ ()\\-]){0,2}\\d)+")

    private const val CURRENCY_SYMBOLS = "$\u20AC\u00A3\u00A5\u20B9\u20A9\u20BD\u20BA\u0E3F"
    private const val CURRENCY_CODES =
        "usd|eur|gbp|aud|cad|nzd|inr|jpy|chf|cny|sgd|hkd|zar|sek|nok|dkk|aed|rs|rm"
    private val MONEY_BEFORE = Regex(
        "(?:[$CURRENCY_SYMBOLS]|\\b(?:$CURRENCY_CODES)\\.?)\\s?$", RegexOption.IGNORE_CASE,
    )
    private val MONEY_AFTER = Regex(
        "^\\s?(?:[$CURRENCY_SYMBOLS]|(?:$CURRENCY_CODES|dollars?|euros?|pounds?|rupees?)\\b)",
        RegexOption.IGNORE_CASE,
    )
    private val DECIMAL_AFTER = Regex("^[.,]\\d{1,2}(?!\\d)")
    private val DECIMAL_BEFORE = Regex("\\d[.,]$")

    private const val MONTHS = "jan|feb|mar|apr|may|jun|jul|aug|sep|sept|oct|nov|dec"
    private val MONTH_BEFORE = Regex(
        "\\b(?:$MONTHS)[a-z]*\\.?,?\\s+(?:\\d{1,2}(?:st|nd|rd|th)?,?\\s+)?$", RegexOption.IGNORE_CASE,
    )
    private val MONTH_AFTER = Regex("^\\s+(?:$MONTHS)[a-z]*\\b", RegexOption.IGNORE_CASE)
    private val DATE_BEFORE = Regex("\\d{1,2}[/.\\-]\\d{1,2}[/.\\-]$")
    private val DATE_AFTER = Regex("^[/.\\-]\\d{1,2}[/.\\-]\\d{1,2}(?!\\d)")

    private val ABBREVIATIONS = setOf("rs", "no", "nr", "ref", "inr", "acct", "txn", "tel", "ph", "st")

    /**
     * Returns [text] with likely one-time codes replaced by asterisks.
     *
     * [context] is the notification's other field (the title when [text] is the body, and the
     * other way round); [contextBefore] says whether it is read before [text]. A keyword that
     * only appears in the context counts as sitting at that edge of [text].
     */
    fun redact(text: String, context: String = "", contextBefore: Boolean = true): String {
        if (text.isEmpty()) return text
        val mask = ArrayList<IntRange>()
        PREFIXED.findAll(text).forEach { m -> m.groups[1]?.range?.let(mask::add) }

        val keywords = KEYWORDS.findAll(text).map { it.range }.toList()
        val contextKeyword = context.isNotEmpty() && KEYWORDS.containsMatchIn(context)
        if (keywords.isNotEmpty() || contextKeyword) {
            val phoneRuns = DIGIT_RUN.findAll(text).filter(::isPhoneShaped).map { it.range }.toList()
            for (m in CANDIDATE.findAll(text)) {
                val r = m.range
                if (mask.any { it.first <= r.first && r.last <= it.last }) continue
                if (phoneRuns.any { it.first <= r.first && r.last <= it.last }) continue
                if (isMoney(text, r) || isYear(text, r, m.value)) continue
                if (nearKeyword(text, r, keywords, contextKeyword, contextBefore)) mask += r
            }
        }
        if (mask.isEmpty()) return text

        val out = StringBuilder(text.length)
        var index = 0
        for (r in mask.sortedBy { it.first }) {
            if (r.first < index) continue
            out.append(text, index, r.first)
            repeat(text.substring(r.first, r.last + 1).count(Char::isDigit)) { out.append('*') }
            index = r.last + 1
        }
        out.append(text, index, text.length)
        return out.toString()
    }

    private fun isPhoneShaped(run: MatchResult): Boolean {
        val digits = run.value.count(Char::isDigit)
        return digits >= 9 || (run.value.startsWith("+") && digits >= 7)
    }

    private fun isMoney(text: String, r: IntRange): Boolean {
        val before = text.substring((r.first - 6).coerceAtLeast(0), r.first)
        val after = text.substring(r.last + 1, (r.last + 10).coerceAtMost(text.length))
        return MONEY_BEFORE.containsMatchIn(before) || MONEY_AFTER.containsMatchIn(after) ||
            DECIMAL_AFTER.containsMatchIn(after) || DECIMAL_BEFORE.containsMatchIn(before)
    }

    private fun isYear(text: String, r: IntRange, value: String): Boolean {
        if (value.length != 4 || value.toInt() !in 1900..2099) return false
        val before = text.substring((r.first - 24).coerceAtLeast(0), r.first)
        val after = text.substring(r.last + 1, (r.last + 14).coerceAtMost(text.length))
        return MONTH_BEFORE.containsMatchIn(before) || DATE_BEFORE.containsMatchIn(before) ||
            MONTH_AFTER.containsMatchIn(after) || DATE_AFTER.containsMatchIn(after)
    }

    private fun nearKeyword(
        text: String,
        r: IntRange,
        keywords: List<IntRange>,
        contextKeyword: Boolean,
        contextBefore: Boolean,
    ): Boolean {
        val start = r.first
        val end = r.last + 1
        for (k in keywords) {
            val kEnd = k.last + 1
            val near = when {
                kEnd <= start -> start - kEnd <= MAX_DISTANCE && !sentenceBreak(text, kEnd, start)
                k.first >= end -> k.first - end <= MAX_DISTANCE && !sentenceBreak(text, end, k.first)
                else -> true
            }
            if (near) return true
        }
        if (!contextKeyword) return false
        return if (contextBefore) {
            start <= MAX_DISTANCE && !sentenceBreak(text, 0, start)
        } else {
            text.length - end <= MAX_DISTANCE && !sentenceBreak(text, end, text.length)
        }
    }

    /**
     * True if a sentence ends between [from] and [to]: `.`, `!` or `?` followed by whitespace
     * and then an upper-case letter, unless the period belongs to an abbreviation ("Rs. 500").
     */
    private fun sentenceBreak(text: String, from: Int, to: Int): Boolean {
        for (i in from until to) {
            val c = text[i]
            if (c != '.' && c != '!' && c != '?') continue
            var next = i + 1
            if (next >= text.length || !text[next].isWhitespace()) continue
            while (next < text.length && text[next].isWhitespace()) next++
            if (next >= text.length || !text[next].isUpperCase()) continue
            if (c == '.' && isAbbreviation(text, i)) continue
            return true
        }
        return false
    }

    private fun isAbbreviation(text: String, periodIndex: Int): Boolean {
        var start = periodIndex
        while (start > 0 && text[start - 1].isLetter()) start--
        return text.substring(start, periodIndex).lowercase() in ABBREVIATIONS
    }
}
