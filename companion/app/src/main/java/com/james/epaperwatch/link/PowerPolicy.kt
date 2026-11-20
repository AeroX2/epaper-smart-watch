package com.james.epaperwatch.link

import kotlin.math.abs

/**
 * The decisions that keep the phone and, above all, the watch asleep: what is worth a message,
 * how often something may repeat, and how reconnects back off. Every message the phone sends
 * wakes the watch's CPU and radio, so "send on meaningful change only" lives here.
 *
 * Pure JVM code (unit tested in `PowerPolicyTest`); the callers supply the clocks.
 */
object PowerPolicy {
    // ------------------------------------------------------------------ periodic work

    /** Calendar re-evaluation and time resync while connected. */
    const val TICK_MS = 60L * 60 * 1000

    /**
     * TIME is resent when the last ACKed one is older than this. TIME carries the next
     * daylight-saving change and is also sent on every time / date / zone broadcast, so this
     * only corrects the drift of the watch's clock.
     */
    const val TIME_RESYNC_MS = 24L * 60 * 60 * 1000

    // ------------------------------------------------------------------ PHONE

    /** A battery change smaller than this is not worth waking the watch for. */
    const val PHONE_BATTERY_STEP = 5

    /**
     * Whether a PHONE message with [newBody] (`battery|charging|dnd`) should replace
     * [shownBody], the one the watch has (or is about to get). Charging and Do Not Disturb
     * changes always go out; the battery level only in steps of [PHONE_BATTERY_STEP] points,
     * and when it reaches 100.
     */
    fun phoneWorthSending(shownBody: String?, newBody: String): Boolean {
        val shown = shownBody?.split('|') ?: return true
        val now = newBody.split('|')
        if (shown.size < 3 || now.size < 3) return true
        if (shown[1] != now[1] || shown[2] != now[2]) return true
        val before = shown[0].toIntOrNull() ?: return true
        val level = now[0].toIntOrNull() ?: return true
        if (level == before) return false
        return level >= 100 || abs(level - before) >= PHONE_BATTERY_STEP
    }

    // ------------------------------------------------------------------ rate limits

    /** A notification that keeps editing itself is forwarded at most this often. */
    const val NOTIFY_UPDATE_MIN_MS = 10_000L

    /**
     * How long to wait before something last sent at [lastMs] may be sent again when at least
     * [minIntervalMs] must lie between two sends. 0 = now. [lastMs] 0 means "never sent".
     * Both times are on the same monotonic clock.
     */
    fun delayUntilAllowed(nowMs: Long, lastMs: Long, minIntervalMs: Long): Long {
        if (lastMs <= 0L || nowMs < lastMs) return 0L
        return (lastMs + minIntervalMs - nowMs).coerceAtLeast(0L)
    }

    /** Doubling backoff, capped. */
    fun nextBackoff(currentMs: Long, maxMs: Long): Long = (currentMs * 2).coerceIn(1L, maxMs)

    // ------------------------------------------------------------------ MUSIC

    /** The watch advances the position itself; a difference up to this is not a seek. */
    const val MUSIC_POSITION_TOLERANCE_S = 3L

    /** Position-only corrections (seeks, drift) are at least this far apart. */
    const val MUSIC_POSITION_MIN_INTERVAL_MS = 15_000L

    const val MUSIC_NOTHING = -1L

    /**
     * Whether the watch needs a new MUSIC message: [MUSIC_NOTHING] = no, 0 = send now, a
     * positive value = look again after that many milliseconds.
     *
     * A different track, duration or play state is sent at once. With those unchanged
     * ([sameTrackAndState]) only a position that is off by more than
     * [MUSIC_POSITION_TOLERANCE_S] ([driftSeconds], against where the watch has counted to)
     * matters, and such corrections are rate-limited: a player that reports its position every
     * second, or plays faster than real time, must not cause a message per callback.
     * [sincePositionFixMs] is the time since the last position-only message
     * (`Long.MAX_VALUE` = none yet).
     */
    fun musicSendDelayMs(
        sameTrackAndState: Boolean,
        nothingPlaying: Boolean,
        driftSeconds: Long,
        sincePositionFixMs: Long,
    ): Long {
        if (!sameTrackAndState) return 0L
        if (nothingPlaying || abs(driftSeconds) <= MUSIC_POSITION_TOLERANCE_S) return MUSIC_NOTHING
        if (sincePositionFixMs < 0L) return 0L
        return (MUSIC_POSITION_MIN_INTERVAL_MS - sincePositionFixMs).coerceAtLeast(0L)
    }

    // ------------------------------------------------------------------ state resend

    /** First delay before a dropped state message is rebuilt and sent again. */
    const val STATE_RETRY_MS = 5_000L
    const val STATE_RETRY_MAX_MS = 5L * 60 * 1000

    // ------------------------------------------------------------------ reconnect

    const val INITIAL_RECONNECT_MS = 1_000L

    /** Cap of the reconnect backoff after an error. */
    const val MAX_RECONNECT_MS = 5L * 60 * 1000

    /**
     * Cap after a plain loss of the radio link (out of range). The reconnect is a passive
     * auto-connect that costs nothing while the watch is away, so it is not held back for
     * long; the backoff only damps a link that flaps at the edge of range, where every
     * reconnect makes the phone send the whole session state again.
     */
    const val LINK_LOSS_MAX_RECONNECT_MS = 60_000L

    /** A session that lasted this long proves the link works: the backoff starts over. */
    const val STABLE_SESSION_MS = 60_000L

    /**
     * Connections in a row that were established but did not lead to a stable session (setup
     * or handshake failed, the watch stopped answering or closed the link). After this many
     * the phone stops reconnecting until the user, a presence event or Bluetooth being
     * switched on asks again: each attempt costs the watch a connection and a handshake.
     */
    const val MAX_FAILED_CONNECTIONS = 8

    /** Failed pairings in a row before the phone stops trying (each may show a dialog). */
    const val MAX_PAIRING_FAILURES = 3

    /** The delay for the next reconnect, given the backoff state and why the link ended. */
    fun reconnectDelayMs(backoffMs: Long, linkLoss: Boolean): Long =
        backoffMs.coerceIn(INITIAL_RECONNECT_MS, if (linkLoss) LINK_LOSS_MAX_RECONNECT_MS else MAX_RECONNECT_MS)

    fun isStableSession(sessionAgeMs: Long?): Boolean =
        sessionAgeMs != null && sessionAgeMs >= STABLE_SESSION_MS
}
