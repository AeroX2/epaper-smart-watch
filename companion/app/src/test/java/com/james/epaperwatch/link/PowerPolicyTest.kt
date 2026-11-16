package com.james.epaperwatch.link

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class PowerPolicyTest {
    // ------------------------------------------------------------------ PHONE

    @Test
    fun phoneIsSentWhenTheWatchHasNothingYet() {
        assertTrue(PowerPolicy.phoneWorthSending(null, "80|0|0"))
        assertTrue(PowerPolicy.phoneWorthSending("garbage", "80|0|0"))
        assertTrue(PowerPolicy.phoneWorthSending("x|0|0", "80|0|0"))
    }

    @Test
    fun phoneBatteryMovesInFivePointSteps() {
        assertFalse(PowerPolicy.phoneWorthSending("80|0|0", "80|0|0"))
        for (level in 76..84) {
            assertFalse("level $level", PowerPolicy.phoneWorthSending("80|0|0", "$level|0|0"))
        }
        assertTrue(PowerPolicy.phoneWorthSending("80|0|0", "75|0|0"))
        assertTrue(PowerPolicy.phoneWorthSending("80|0|0", "85|0|0"))
        assertTrue(PowerPolicy.phoneWorthSending("80|0|0", "40|0|0"))
    }

    @Test
    fun phoneBatteryStepsAreMeasuredFromWhatTheWatchShows() {
        // Discharging 80 -> 70 one percent at a time: only 75 and 70 go out.
        var shown = "80|0|0"
        val sent = ArrayList<Int>()
        for (level in 79 downTo 70) {
            val body = "$level|0|0"
            if (PowerPolicy.phoneWorthSending(shown, body)) {
                sent += level
                shown = body
            }
        }
        assertEquals(listOf(75, 70), sent)
    }

    @Test
    fun phoneReportsAFullBattery() {
        assertTrue(PowerPolicy.phoneWorthSending("97|1|0", "100|1|0"))
        assertFalse(PowerPolicy.phoneWorthSending("100|1|0", "100|1|0"))
        assertFalse(PowerPolicy.phoneWorthSending("100|0|0", "97|0|0"))
        assertTrue(PowerPolicy.phoneWorthSending("100|0|0", "95|0|0"))
    }

    @Test
    fun phoneChargingAndDndChangesAlwaysGoOut() {
        assertTrue(PowerPolicy.phoneWorthSending("80|0|0", "80|1|0"))
        assertTrue(PowerPolicy.phoneWorthSending("80|1|0", "80|0|0"))
        assertTrue(PowerPolicy.phoneWorthSending("80|0|0", "80|0|1"))
        assertTrue(PowerPolicy.phoneWorthSending("80|0|1", "81|0|0"))
    }

    // ------------------------------------------------------------------ rate limit, backoff

    @Test
    fun delayUntilAllowed() {
        assertEquals(0L, PowerPolicy.delayUntilAllowed(5_000L, 0L, 10_000L)) // never sent
        assertEquals(10_000L, PowerPolicy.delayUntilAllowed(5_000L, 5_000L, 10_000L))
        assertEquals(4_000L, PowerPolicy.delayUntilAllowed(11_000L, 5_000L, 10_000L))
        assertEquals(0L, PowerPolicy.delayUntilAllowed(15_000L, 5_000L, 10_000L))
        assertEquals(0L, PowerPolicy.delayUntilAllowed(99_000L, 5_000L, 10_000L))
        assertEquals(0L, PowerPolicy.delayUntilAllowed(1_000L, 5_000L, 10_000L)) // clock went back
    }

    @Test
    fun aNotificationEditedEverySecondIsSentOncePerInterval() {
        var lastSent = 0L
        var sends = 0
        // One edit per second for a minute, starting at t = 100 s; a deferred send is modelled
        // by sending at the first edit on or after the moment the limit runs out.
        for (second in 100 until 160) {
            val now = second * 1000L
            if (PowerPolicy.delayUntilAllowed(now, lastSent, PowerPolicy.NOTIFY_UPDATE_MIN_MS) == 0L) {
                sends++
                lastSent = now
            }
        }
        assertEquals(6, sends)
    }

    @Test
    fun backoffDoublesUpToTheCap() {
        var delay = PowerPolicy.STATE_RETRY_MS
        val seen = ArrayList<Long>()
        repeat(9) {
            seen += delay
            delay = PowerPolicy.nextBackoff(delay, PowerPolicy.STATE_RETRY_MAX_MS)
        }
        assertEquals(
            listOf(5_000L, 10_000L, 20_000L, 40_000L, 80_000L, 160_000L, 300_000L, 300_000L, 300_000L),
            seen,
        )
        assertEquals(1L, PowerPolicy.nextBackoff(0L, 1_000L)) // never a zero delay
    }

    // ------------------------------------------------------------------ MUSIC

    @Test
    fun musicTrackOrStateChangeIsSentAtOnce() {
        assertEquals(0L, PowerPolicy.musicSendDelayMs(false, false, 0L, 0L))
        assertEquals(0L, PowerPolicy.musicSendDelayMs(false, true, 0L, Long.MAX_VALUE))
    }

    @Test
    fun musicPlayingOnIsNotAMessage() {
        // The position the player reports matches what the watch has counted to.
        for (drift in -3L..3L) {
            assertEquals(
                PowerPolicy.MUSIC_NOTHING,
                PowerPolicy.musicSendDelayMs(true, false, drift, Long.MAX_VALUE),
            )
        }
        // Nothing playing, before and now.
        assertEquals(PowerPolicy.MUSIC_NOTHING, PowerPolicy.musicSendDelayMs(true, true, 50L, Long.MAX_VALUE))
    }

    @Test
    fun musicSeekIsSentButRateLimited() {
        // The first seek goes out now.
        assertEquals(0L, PowerPolicy.musicSendDelayMs(true, false, 40L, Long.MAX_VALUE))
        assertEquals(0L, PowerPolicy.musicSendDelayMs(true, false, -40L, 60_000L))
        // Another one 4 s after the last correction waits for the rest of the interval.
        assertEquals(
            PowerPolicy.MUSIC_POSITION_MIN_INTERVAL_MS - 4_000L,
            PowerPolicy.musicSendDelayMs(true, false, 40L, 4_000L),
        )
        assertEquals(0L, PowerPolicy.musicSendDelayMs(true, false, 4L, PowerPolicy.MUSIC_POSITION_MIN_INTERVAL_MS))
    }

    // ------------------------------------------------------------------ WEATHER

    @Test
    fun weatherIsNotFetchedWhileTheForecastIsFresh() {
        val minute = 60_000L
        assertTrue(PowerPolicy.weatherFetchDue(null, Long.MAX_VALUE)) // no forecast yet
        assertFalse(PowerPolicy.weatherFetchDue(null, 5 * minute)) // reconnect shortly after a fetch
        assertFalse(PowerPolicy.weatherFetchDue(20 * minute, 20 * minute))
        assertTrue(PowerPolicy.weatherFetchDue(60 * minute, 60 * minute)) // the hourly tick
        assertTrue(PowerPolicy.weatherFetchDue(null, 3 * 60 * minute))
        assertTrue(PowerPolicy.weatherFetchDue(null, -minute)) // clock was set back: stale
    }

    @Test
    fun weatherFailuresAreNotRetriedAtOnce() {
        val minute = 60_000L
        assertFalse(PowerPolicy.weatherFetchDue(2 * minute, Long.MAX_VALUE))
        assertFalse(PowerPolicy.weatherFetchDue(0L, 3 * 60 * minute))
        assertTrue(PowerPolicy.weatherFetchDue(10 * minute, Long.MAX_VALUE))
    }

    @Test
    fun theTickOutlastsTheFreshnessWindow() {
        // Otherwise the hourly tick would find the forecast it fetched an hour ago "fresh".
        assertTrue(PowerPolicy.TICK_MS > PowerPolicy.WEATHER_FRESH_MS)
        assertTrue(PowerPolicy.TICK_MS >= PowerPolicy.WEATHER_MIN_ATTEMPT_MS)
    }

    // ------------------------------------------------------------------ reconnect

    @Test
    fun reconnectDelayIsCappedByTheReasonTheLinkEnded() {
        assertEquals(1_000L, PowerPolicy.reconnectDelayMs(PowerPolicy.INITIAL_RECONNECT_MS, linkLoss = true))
        assertEquals(1_000L, PowerPolicy.reconnectDelayMs(0L, linkLoss = false))
        assertEquals(32_000L, PowerPolicy.reconnectDelayMs(32_000L, linkLoss = true))
        // Out of range: never held back for longer than a minute.
        assertEquals(60_000L, PowerPolicy.reconnectDelayMs(256_000L, linkLoss = true))
        // Errors: up to five minutes.
        assertEquals(256_000L, PowerPolicy.reconnectDelayMs(256_000L, linkLoss = false))
        assertEquals(300_000L, PowerPolicy.reconnectDelayMs(9_999_999L, linkLoss = false))
    }

    @Test
    fun failedConnectionsEndWithinMinutesNotInALoop() {
        // The delays before each retry until the link gives up.
        var backoff = PowerPolicy.INITIAL_RECONNECT_MS
        var total = 0L
        repeat(PowerPolicy.MAX_FAILED_CONNECTIONS - 1) {
            total += PowerPolicy.reconnectDelayMs(backoff, linkLoss = false)
            backoff = PowerPolicy.nextBackoff(backoff, PowerPolicy.MAX_RECONNECT_MS)
        }
        assertEquals(127_000L, total)
    }

    @Test
    fun onlyASessionThatLastedResetsTheBackoff() {
        assertFalse(PowerPolicy.isStableSession(null))
        assertFalse(PowerPolicy.isStableSession(0L))
        assertFalse(PowerPolicy.isStableSession(PowerPolicy.STABLE_SESSION_MS - 1))
        assertTrue(PowerPolicy.isStableSession(PowerPolicy.STABLE_SESSION_MS))
    }
}
