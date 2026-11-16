package com.james.epaperwatch.health

import java.time.Instant
import java.time.LocalDate
import java.time.ZoneId
import java.time.ZoneOffset
import java.time.format.DateTimeFormatter
import java.time.format.DateTimeParseException
import java.util.Locale

/**
 * The time span of one local day's step record: local midnight to min(now, next midnight).
 * Pure java.time code (unit tested in `StepWindowTest`).
 */
class StepWindow private constructor(
    val day: LocalDate,
    val start: Instant,
    val end: Instant,
    val startOffset: ZoneOffset,
    val endOffset: ZoneOffset,
    /** True once the day is over, i.e. [end] is the next local midnight. */
    val complete: Boolean,
) {
    /** `yyyymmdd`, as the watch writes days in HISTORY. */
    val key: String get() = FORMAT.format(day)

    companion object {
        private val FORMAT: DateTimeFormatter = DateTimeFormatter.ofPattern("yyyyMMdd", Locale.US)
        const val MAX_STEPS = 1_000_000

        /** Parses the watch's `yyyymmdd`; null if it is not a real date. */
        fun parseDay(yyyymmdd: String): LocalDate? = try {
            if (yyyymmdd.length == 8) LocalDate.parse(yyyymmdd, FORMAT) else null
        } catch (e: DateTimeParseException) {
            null
        }

        /**
         * The window for [day] in [zone] as seen at [now], or null when nothing may be
         * written: the day has not started yet (or [now] is exactly its first instant), so
         * the end would not be after the start.
         */
        fun of(day: LocalDate, now: Instant, zone: ZoneId): StepWindow? {
            val start = day.atStartOfDay(zone).toInstant()
            val nextMidnight = day.plusDays(1).atStartOfDay(zone).toInstant()
            val complete = !now.isBefore(nextMidnight)
            val end = if (complete) nextMidnight else now
            if (!end.isAfter(start)) return null
            val rules = zone.rules
            // The end offset is that of the last instant inside the window.
            return StepWindow(
                day, start, end, rules.getOffset(start), rules.getOffset(end.minusMillis(1)), complete,
            )
        }

        /** A record is only written for a real, positive count within Health Connect's range. */
        fun isWritable(steps: Int): Boolean = steps in 1..MAX_STEPS
    }
}
