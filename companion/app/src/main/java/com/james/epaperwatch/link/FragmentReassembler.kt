package com.james.epaperwatch.link

import com.james.epaperwatch.protocol.Protocol
import java.io.ByteArrayOutputStream

/**
 * Watch -> phone reassembly (PROTOCOL.md "Fragmentation"):
 * a notification starting with `+` is a non-final chunk, `=` the final chunk, anything else
 * a complete message. A plain message arriving while a partial is pending discards the
 * partial. [reset] must be called on disconnect.
 *
 * Pure JVM code (unit tested); not thread-safe, used from the link's handler thread.
 */
class FragmentReassembler {
    private val pending = ByteArrayOutputStream()
    /** The partial grew past the protocol's message limit: swallow chunks up to its `=`. */
    private var overflowed = false

    /** Feed one notification; returns a complete message, or null if more chunks are needed. */
    fun accept(notification: ByteArray): ByteArray? {
        if (notification.isEmpty()) return null
        return when (notification[0]) {
            PLUS -> {
                append(notification)
                null
            }
            EQUALS -> {
                // A lone `=` (no `+` before it) is accepted as a one-chunk message.
                append(notification)
                val complete = if (overflowed) null else pending.toByteArray()
                reset()
                complete?.takeIf { it.isNotEmpty() }
            }
            else -> {
                reset() // a plain message discards any pending partial
                notification
            }
        }
    }

    fun reset() {
        pending.reset()
        overflowed = false
    }

    private fun append(chunk: ByteArray) {
        if (overflowed) return
        if (pending.size() + chunk.size - 1 > Protocol.MAX_MESSAGE_BYTES) {
            overflowed = true
            pending.reset()
            return
        }
        pending.write(chunk, 1, chunk.size - 1)
    }

    private companion object {
        const val PLUS: Byte = 0x2B
        const val EQUALS: Byte = 0x3D
    }
}
