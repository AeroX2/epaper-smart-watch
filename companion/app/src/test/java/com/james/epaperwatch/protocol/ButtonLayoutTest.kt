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
        // Top left becomes Next; top right (which had Next) takes Select.
        assertEquals(listOf(1, 3, 0, 2), Protocol.assignButton(Protocol.DEFAULT_BUTTONS, 0, 1))
        assertEquals(Protocol.DEFAULT_BUTTONS, Protocol.assignButton(Protocol.DEFAULT_BUTTONS, 0, 3))
    }

    @Test
    fun theFirstFormatsDefaultIsTodaysDefault() {
        // Same buttons, same roles: only the corner names were wrong.
        assertEquals(Protocol.DEFAULT_BUTTONS, Protocol.buttonsFromFirstFormat(listOf(1, 2, 3, 0)))
    }

    @Test
    fun theMessageListsRolesByCorner() {
        val message = Out.buttons(listOf(3, 2, 1, 0))
        assertEquals("BUTTONS", message.type)
        assertEquals(listOf("3", "2", "1", "0"), message.fields)
    }
}
