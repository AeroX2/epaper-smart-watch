package com.james.epaperwatch.ota

import com.james.epaperwatch.protocol.Protocol
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.util.zip.CRC32

class FirmwareImageTest {
    private fun image(size: Int = 64, stack: Long = 0x20030000L, reset: Long = 0x08010101L): ByteArray {
        val b = ByteArray(size) { (it * 7).toByte() }
        putWord(b, 0, stack)
        putWord(b, 4, reset)
        return b
    }

    private fun putWord(b: ByteArray, at: Int, v: Long) {
        for (i in 0..3) b[at + i] = (v shr (8 * i)).toByte()
    }

    private fun suffix(): ByteArray {
        val s = ByteArray(16)
        s[8] = 'U'.code.toByte()
        s[9] = 'F'.code.toByte()
        s[10] = 'D'.code.toByte()
        s[11] = 16
        return s
    }

    private fun ok(result: FirmwareImage.Result) = (result as FirmwareImage.Result.Ok).image

    @Test fun validImage() {
        val bytes = image()
        val parsed = ok(FirmwareImage.parse("firmware.bin", bytes))
        assertEquals(64, parsed.size)
        assertEquals(CRC32().apply { update(bytes) }.value, parsed.crc32)
        assertFalse(parsed.suffixStripped)
        assertEquals("firmware", parsed.versionLabel)
    }

    @Test fun dfuSuffixIsStripped() {
        val bytes = image()
        val parsed = ok(FirmwareImage.parse("fw.bin", bytes + suffix()))
        assertTrue(parsed.suffixStripped)
        assertEquals(64, parsed.size)
        assertEquals(CRC32().apply { update(bytes) }.value, parsed.crc32)
    }

    @Test fun crcOfKnownVector() {
        // CRC-32 (IEEE) of "123456789" is 0xCBF43926.
        assertEquals(0xCBF43926L, CRC32().apply { update("123456789".toByteArray()) }.value)
    }

    @Test fun sizeLimits() {
        assertTrue(FirmwareImage.parse("a", image(8)) is FirmwareImage.Result.Ok)
        assertTrue(FirmwareImage.parse("a", image(8).copyOf(7)) is FirmwareImage.Result.Invalid)
        assertTrue(FirmwareImage.parse("a", image(Protocol.OTA_MAX_IMAGE_BYTES)) is FirmwareImage.Result.Ok)
        assertTrue(
            FirmwareImage.parse("a", image(Protocol.OTA_MAX_IMAGE_BYTES + 1)) is FirmwareImage.Result.Invalid,
        )
    }

    @Test fun stackPointerMustBeInRam() {
        assertTrue(FirmwareImage.parse("a", image(stack = 0x20000000L)) is FirmwareImage.Result.Ok)
        assertTrue(FirmwareImage.parse("a", image(stack = 0x20030004L)) is FirmwareImage.Result.Invalid)
        assertTrue(FirmwareImage.parse("a", image(stack = 0x1FFFFFFCL)) is FirmwareImage.Result.Invalid)
    }

    @Test fun resetVectorMustBeThumbInsideApplication() {
        assertTrue(FirmwareImage.parse("a", image(reset = 0x08010001L)) is FirmwareImage.Result.Ok)
        assertTrue(FirmwareImage.parse("a", image(reset = 0x080C9FFFL)) is FirmwareImage.Result.Ok)
        // Not thumb.
        assertTrue(FirmwareImage.parse("a", image(reset = 0x08010100L)) is FirmwareImage.Result.Invalid)
        // A bootloader build (linked for 0x08000000).
        assertTrue(FirmwareImage.parse("a", image(reset = 0x08000101L)) is FirmwareImage.Result.Invalid)
        assertTrue(FirmwareImage.parse("a", image(reset = 0x080CA001L)) is FirmwareImage.Result.Invalid)
    }
}
