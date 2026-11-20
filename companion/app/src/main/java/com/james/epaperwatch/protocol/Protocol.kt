package com.james.epaperwatch.protocol

import java.util.UUID

/** GATT layout and limits from PROTOCOL.md (v2). */
object Protocol {
    const val VERSION = 2
    const val MAX_MESSAGE_BYTES = 160
    const val REQUESTED_MTU = 185
    const val ACK_TIMEOUT_MS = 1_500L
    const val MAX_RETRIES = 3
    const val FULL_BACKOFF_MS = 500L
    const val MAX_SESSION_NOTIFICATIONS = 5

    val SERVICE_UUID: UUID = UUID.fromString("7BD10000-6B10-4A21-9D6A-3A56B35C1000")
    val PHONE_TO_WATCH_UUID: UUID = UUID.fromString("7BD10001-6B10-4A21-9D6A-3A56B35C1000")
    val WATCH_TO_PHONE_UUID: UUID = UUID.fromString("7BD10002-6B10-4A21-9D6A-3A56B35C1000")
    val OTA_DATA_UUID: UUID = UUID.fromString("7BD10003-6B10-4A21-9D6A-3A56B35C1000")
    val CCC_UUID: UUID = UUID.fromString("00002902-0000-1000-8000-00805F9B34FB")

    /** `HELLO.flags` bits (watch -> phone). */
    const val FLAG_SECURE = 1
    const val FLAG_OTA = 2
    const val FLAG_STORAGE = 4

    /** Alert tone names; the index is the wire value (`SETTINGS.tone`, `TONE_PREVIEW`). */
    val TONE_NAMES = listOf("None", "Chirp", "Double", "Rise", "Fall", "Triple")

    // Firmware update over Bluetooth.
    const val OTA_MIN_IMAGE_BYTES = 8
    const val OTA_MAX_IMAGE_BYTES = 761_856
    const val OTA_MAX_CHUNK = 240
    /**
     * Largest chunk for a watch that does not announce big-packet support (`ACCEPT|n|1`).
     * Firmware before that fix drops any packet over 128 bytes from its radio core: 96 data
     * bytes + offset + ATT and L2CAP headers stay below that.
     */
    const val OTA_SAFE_CHUNK = 96
    /** ATT write header (3) plus the chunk's 4-byte offset. */
    const val OTA_CHUNK_OVERHEAD = 7
}

/**
 * A phone -> watch message without its sequence number. [coalesceKey] lets the reliable
 * channel replace a still-queued message that has been superseded (e.g. two TIME updates).
 */
class OutMessage(
    val type: String,
    val fields: List<String> = emptyList(),
    val coalesceKey: String? = null,
    /**
     * If set, the reliable channel calls this at every transmission attempt (the first send
     * and each retry) and sends the returned message instead, so a time-sensitive payload
     * (TIME) is never stale on the air. The seq stays the same.
     */
    val refresh: (() -> OutMessage)? = null,
) {
    /** Field payload, used for de-duplicating state messages. */
    val body: String get() = fields.joinToString("|")

    fun encode(seq: Int): ByteArray {
        val text = StringBuilder().append(seq).append('|').append(type)
        for (field in fields) text.append('|').append(field)
        // Field limits keep every message well under 160 bytes; this is only a backstop.
        val bounded = if (text.length > Protocol.MAX_MESSAGE_BYTES) {
            text.substring(0, Protocol.MAX_MESSAGE_BYTES)
        } else {
            text.toString()
        }
        return bounded.toByteArray(Charsets.US_ASCII)
    }

    /**
     * Called when this message replaces [old], which was still queued under the same
     * coalesce key. A silent NOTIFY update must not swallow the alert of the not-yet-sent
     * new-arrival NOTIFY it replaces.
     */
    fun superseding(old: OutMessage): OutMessage {
        if (type == "NOTIFY" && old.type == "NOTIFY" && fields.size == 5 &&
            fields[4] == "0" && old.fields.getOrNull(4) == "1"
        ) {
            return OutMessage(type, fields.dropLast(1) + "1", coalesceKey)
        }
        return this
    }

    override fun toString(): String = if (fields.isEmpty()) type else "$type|$body"
}

/** Builders for every phone -> watch message, applying the spec's field limits. */
object Out {
    private fun t(value: CharSequence?, max: Int) = WireText.sanitize(value, max)
    private fun b(value: Boolean) = if (value) "1" else "0"

    fun hello(appVersion: String) =
        OutMessage("HELLO", listOf(Protocol.VERSION.toString(), t(appVersion, 20)))

    /**
     * [transitionUtc] / [offsetAfter]: the zone's next offset change (DST), 0 = none.
     * [refresh] rebuilds the message at each transmission attempt.
     */
    fun time(
        utcSeconds: Long,
        offsetSeconds: Int,
        transitionUtc: Long = 0L,
        offsetAfter: Int = 0,
        refresh: (() -> OutMessage)? = null,
    ) = OutMessage(
        "TIME",
        listOf(
            utcSeconds.toString(),
            offsetSeconds.toString(),
            transitionUtc.coerceIn(0L, Int.MAX_VALUE.toLong()).toString(),
            offsetAfter.toString(),
        ),
        "TIME",
        refresh,
    )

    /** [alert]: true only for a genuinely new arrival (the watch vibrates / plays its tone). */
    fun notify(id: Int, app: CharSequence?, title: CharSequence?, text: CharSequence?, alert: Boolean) =
        OutMessage(
            "NOTIFY",
            listOf(id.toString(), t(app, 24), t(title, 28), t(text, 56), b(alert)),
            "NOTIFY:$id",
        )

    fun notifyClear(id: Int) = OutMessage("NOTIFY_CLEAR", listOf(id.toString()), "NOTIFY:$id")

    fun callRing(caller: CharSequence?) = OutMessage("CALL", listOf("RING", t(caller, 28)), "CALL")
    fun callEnd() = OutMessage("CALL", listOf("END", ""), "CALL")

    /** state: 0 none, 1 playing, 2 paused. */
    fun music(state: Int, position: Long, duration: Long, title: CharSequence?, artist: CharSequence?) =
        if (state == 0) {
            OutMessage("MUSIC", listOf("0", "0", "0", "", ""), "MUSIC")
        } else {
            OutMessage(
                "MUSIC",
                listOf(
                    state.toString(),
                    position.coerceAtLeast(0).toString(),
                    duration.coerceAtLeast(0).toString(),
                    t(title, 24),
                    t(artist, 24),
                ),
                "MUSIC",
            )
        }

    fun nextAlarm(utcSeconds: Long) =
        OutMessage("NEXT_ALARM", listOf(utcSeconds.coerceAtLeast(0).toString()), "NEXT_ALARM")

    fun event(utcSeconds: Long, title: CharSequence?) =
        if (utcSeconds <= 0) {
            OutMessage("EVENT", listOf("0", ""), "EVENT")
        } else {
            OutMessage("EVENT", listOf(utcSeconds.toString(), t(title, 28)), "EVENT")
        }

    fun phone(battery: Int, charging: Boolean, dnd: Boolean) =
        OutMessage("PHONE", listOf(battery.coerceIn(0, 100).toString(), b(charging), b(dnd)), "PHONE")

    /** [tone] 0..5, or null to leave the watch's tone unchanged (the field is omitted). */
    fun settings(vibration: Boolean, quiet: Boolean, tone: Int?) =
        OutMessage(
            "SETTINGS",
            if (tone != null && tone in 0..5) {
                listOf(b(vibration), b(quiet), tone.toString())
            } else {
                listOf(b(vibration), b(quiet))
            },
            "SETTINGS",
        )

    fun tonePreview(tone: Int) =
        OutMessage("TONE_PREVIEW", listOf(tone.coerceIn(1, 5).toString()), "TONE_PREVIEW")

    fun findWatch() = OutMessage("FIND_WATCH")

    /**
     * The phone's camera screen for the watch's Camera app: [state] is CLOSED, OPENING, READY,
     * COUNTDOWN (number = seconds), SAVED (number = photos since it opened) or ERROR (detail).
     * A newer state replaces one still queued.
     */
    fun camera(state: String, number: Int = 0, detail: String = "") =
        OutMessage(
            "CAMERA",
            listOf(state, number.coerceIn(0, 9999).toString(), t(detail.uppercase(), 20)),
            "CAMERA",
        )
    fun request(what: String) = OutMessage("REQUEST", listOf(what), "REQUEST:$what")
    fun stepsReset() = OutMessage("STEPS_RESET")


    /** [crc32] is the IEEE CRC-32 of the image, sent as unsigned decimal. */
    fun otaBegin(size: Int, crc32: Long, version: CharSequence?) =
        OutMessage(
            "OTA_BEGIN",
            listOf(size.toString(), (crc32 and 0xFFFFFFFFL).toString(), t(version, 16)),
        )
    fun otaEnd() = OutMessage("OTA_END")
    fun otaAbort() = OutMessage("OTA_ABORT")
}

/** A watch -> phone message. Numeric accessors read missing/garbled fields as 0. */
class InMessage(val type: String, val fields: List<String>) {
    fun str(index: Int): String = fields.getOrNull(index)?.trim() ?: ""
    fun int(index: Int): Int = str(index).toIntOrNull() ?: 0
    fun long(index: Int): Long = str(index).toLongOrNull() ?: 0L
    fun has(index: Int): Boolean = str(index).isNotEmpty()

    override fun toString(): String =
        if (fields.isEmpty()) type else type + "|" + fields.joinToString("|")

    companion object {
        fun parse(bytes: ByteArray): InMessage? {
            val text = String(bytes, Charsets.US_ASCII).trim { it == '\u0000' || it.isWhitespace() }
            if (text.isEmpty()) return null
            val parts = text.split('|')
            return InMessage(parts[0].trim(), parts.drop(1))
        }
    }
}
