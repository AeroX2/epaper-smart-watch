package com.james.epaperwatch.protocol

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ButtonLayoutTest {
    @Test
    fun theDefaultIsValid() {
        assertTrue(Protocol.validButtons(Protocol.DEFAULT_BUTTONS))
    }

    @Test
    fun layoutsNeedEveryRoleOnce() {
        assertFalse(Protocol.validButtons(listOf(1, 1, 3, 0)))
        assertFalse(Protocol.validButtons(listOf(1, 2, 3)))
        assertFalse(Protocol.validButtons(listOf(1, 2, 3, 4)))
        assertTrue(Protocol.validButtons(listOf(0, 1, 2, 3)))
    }

    @Test
    fun assigningARoleSwapsItWithTheCornerThatHadIt() {
        // Top left becomes Select; bottom left (which had Select) takes Next.
        assertEquals(listOf(3, 2, 1, 0), Protocol.assignButton(listOf(1, 2, 3, 0), 0, 3))
        assertEquals(listOf(1, 2, 3, 0), Protocol.assignButton(listOf(1, 2, 3, 0), 0, 1))
    }

    @Test
    fun theMessageListsRolesByCorner() {
        val message = Out.buttons(listOf(3, 2, 1, 0))
        assertEquals("BUTTONS", message.type)
        assertEquals(listOf("3", "2", "1", "0"), message.fields)
    }
}
