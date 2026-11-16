package com.james.epaperwatch.health

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.time.LocalDate
import java.time.ZoneId
import java.time.ZonedDateTime

class StepWindowTest {
    private val sydney = ZoneId.of("Australia/Sydney")
    private val day = LocalDate.of(2026, 11, 17)

    private fun at(hour: Int, minute: Int, d: LocalDate = day) =
        ZonedDateTime.of(d.year, d.monthValue, d.dayOfMonth, hour, minute, 0, 0, sydney).toInstant()

    @Test fun todayEndsNow() {
        val w = StepWindow.of(day, at(14, 30), sydney)!!
        assertEquals(at(0, 0), w.start)
        assertEquals(at(14, 30), w.end)
        assertFalse(w.complete)
        assertEquals("20261117", w.key)
    }

    @Test fun finishedDayEndsAtNextMidnight() {
        val w = StepWindow.of(day, at(9, 0, day.plusDays(3)), sydney)!!
        assertEquals(at(0, 0), w.start)
        assertEquals(at(0, 0, day.plusDays(1)), w.end)
        assertTrue(w.complete)
    }

    @Test fun dayThatHasNotStartedIsNotWritten() {
        assertNull(StepWindow.of(day.plusDays(1), at(23, 59), sydney))
        assertNull(StepWindow.of(day, at(0, 0), sydney)) // end would equal start
        assertNotNull(StepWindow.of(day, at(0, 1), sydney))
    }

    @Test fun daylightSavingDayHasBothOffsets() {
        // Sydney clocks go forward on 2026-10-04: the day is 23 hours long.
        val dst = LocalDate.of(2026, 10, 4)
        val w = StepWindow.of(dst, at(12, 0, dst.plusDays(2)), sydney)!!
        assertEquals(23 * 3600L, w.end.epochSecond - w.start.epochSecond)
        assertEquals(10 * 3600, w.startOffset.totalSeconds)
        assertEquals(11 * 3600, w.endOffset.totalSeconds)
    }

    @Test fun parseDay() {
        assertEquals(day, StepWindow.parseDay("20261117"))
        assertNull(StepWindow.parseDay("20261340"))
        assertNull(StepWindow.parseDay("2026111"))
        assertNull(StepWindow.parseDay("abcdefgh"))
    }

    @Test fun zeroStepsAreNeverWritable() {
        assertFalse(StepWindow.isWritable(0))
        assertFalse(StepWindow.isWritable(-5))
        assertTrue(StepWindow.isWritable(1))
        assertFalse(StepWindow.isWritable(StepWindow.MAX_STEPS + 1))
    }
}
