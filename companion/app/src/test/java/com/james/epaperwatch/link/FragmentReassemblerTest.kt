package com.james.epaperwatch.link

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class FragmentReassemblerTest {
    private val r = FragmentReassembler()

    private fun feed(s: String): String? = r.accept(s.toByteArray(Charsets.US_ASCII))?.toString(Charsets.US_ASCII)

    @Test fun plainMessagePassesThrough() = assertEquals("ACK|12", feed("ACK|12"))

    @Test fun fragmentsAreJoined() {
        assertNull(feed("+STATUS|87|4012|1234"))
        assertNull(feed("+5|-"))
        assertEquals("STATUS|87|4012|12345|-1", feed("=1"))
        // State is clean afterwards.
        assertEquals("ACK|1", feed("ACK|1"))
    }

    @Test fun plainMessageDiscardsPartial() {
        assertNull(feed("+HISTORY|2026"))
        assertEquals("ACK|7", feed("ACK|7"))
        // The orphaned tail completes nothing of the old message.
        assertEquals("1117|42", feed("=1117|42"))
    }

    @Test fun resetDropsPartial() {
        assertNull(feed("+HELLO|2|0."))
        r.reset()
        assertNull(feed("+HELLO|2|0.3"))
        assertEquals("HELLO|2|0.3.0|6", feed("=.0|6"))
    }

    @Test fun oversizedMessageIsDropped() {
        repeat(9) { assertNull(feed("+" + "x".repeat(19))) } // 171 bytes > 160
        assertNull(feed("=end"))
        assertEquals("ACK|2", feed("ACK|2"))
        assertNull(feed("+OTA_STATE|PROGRESS"))
        assertEquals("OTA_STATE|PROGRESS|16384", feed("=|16384"))
    }

    @Test fun emptyNotificationIsIgnored() = assertNull(r.accept(ByteArray(0)))
}
