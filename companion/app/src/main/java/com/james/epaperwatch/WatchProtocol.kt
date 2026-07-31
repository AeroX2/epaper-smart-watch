package com.james.epaperwatch

import java.util.UUID

object WatchProtocol {
    val SERVICE_UUID: UUID = UUID.fromString("7BD10000-6B10-4A21-9D6A-3A56B35C1000")
    val PHONE_TO_WATCH_UUID: UUID = UUID.fromString("7BD10001-6B10-4A21-9D6A-3A56B35C1000")
    val WATCH_TO_PHONE_UUID: UUID = UUID.fromString("7BD10002-6B10-4A21-9D6A-3A56B35C1000")
    val CCC_UUID: UUID = UUID.fromString("00002902-0000-1000-8000-00805F9B34FB")

    fun clean(value: CharSequence?, limit: Int): String =
        (value?.toString() ?: "")
            .replace('|', '/')
            .replace(',', ' ')
            .replace('\n', ' ')
            .replace('\r', ' ')
            .trim()
            .take(limit)

    fun time(): String = "TIME,${System.currentTimeMillis() / 1000L}"

    fun notification(app: CharSequence?, title: CharSequence?, text: CharSequence?): String =
        "NOTIFY,${clean(app, 24)}|${clean(title, 28)}|${clean(text, 28)}"

    fun weather(current: Int, high: Int, low: Int, condition: CharSequence?): String =
        "WEATHER,$current,$high,$low|${clean(condition, 20)}"

    fun music(playing: Boolean, positionSeconds: Long, durationSeconds: Long,
              title: CharSequence?, artist: CharSequence?): String =
        "MUSIC,${if (playing) 1 else 0},${positionSeconds.coerceIn(0, 65535)}," +
            "${durationSeconds.coerceIn(0, 65535)}|${clean(title, 24)}|${clean(artist, 24)}"
}
