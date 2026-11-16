package com.james.epaperwatch.ota

import com.james.epaperwatch.protocol.Protocol
import java.util.Locale
import java.util.zip.CRC32

/**
 * A validated watch firmware image (the `firmware.bin` that USB DFU installs, linked for
 * 0x08010000), ready to be sent with `OTA_BEGIN`. Pure JVM code (unit tested).
 */
class FirmwareImage private constructor(
    val name: String,
    val bytes: ByteArray,
    /** IEEE 802.3 CRC-32 of [bytes], unsigned. */
    val crc32: Long,
    /** True if a 16-byte DFU suffix was removed from the file. */
    val suffixStripped: Boolean,
) {
    val size: Int get() = bytes.size
    val crcHex: String get() = String.format(Locale.US, "%08X", crc32)

    /** `OTA_BEGIN.version`: the file name without its extension (the wire cuts it to 16). */
    val versionLabel: String get() = name.substringBeforeLast('.').ifEmpty { name }

    sealed class Result {
        class Ok(val image: FirmwareImage) : Result()
        class Invalid(val reason: String) : Result()
    }

    companion object {
        private const val DFU_SUFFIX_BYTES = 16
        private const val RAM_START = 0x2000_0000L
        private const val RAM_END = 0x2003_0000L
        private const val APP_START = 0x0801_0000L
        private const val APP_END = 0x080C_9FFFL

        /** Largest file worth reading: a full image plus a DFU suffix. */
        const val MAX_FILE_BYTES = Protocol.OTA_MAX_IMAGE_BYTES + DFU_SUFFIX_BYTES

        fun parse(name: String, file: ByteArray): Result {
            val stripped = hasDfuSuffix(file)
            val image = if (stripped) file.copyOf(file.size - DFU_SUFFIX_BYTES) else file
            if (image.size < Protocol.OTA_MIN_IMAGE_BYTES) {
                return Result.Invalid("The file is too small to be a firmware image.")
            }
            if (image.size > Protocol.OTA_MAX_IMAGE_BYTES) {
                return Result.Invalid(
                    "The image is ${image.size} bytes; the watch takes at most " +
                        "${Protocol.OTA_MAX_IMAGE_BYTES}.",
                )
            }
            val stack = word(image, 0)
            if (stack !in RAM_START..RAM_END) {
                return Result.Invalid(
                    "Not a watch firmware image: the initial stack pointer " +
                        "(0x${hex(stack)}) is not in RAM.",
                )
            }
            val reset = word(image, 4)
            if (reset and 1L == 0L || (reset and 1L.inv()) !in APP_START..APP_END) {
                return Result.Invalid(
                    "Not a watch firmware image: the reset vector (0x${hex(reset)}) is not a " +
                        "thumb address inside the application area (is this a bootloader build?).",
                )
            }
            val crc = CRC32().apply { update(image) }.value
            return Result.Ok(FirmwareImage(name, image, crc, stripped))
        }

        /**
         * A DFU suffix is the file's last 16 bytes: ..., "UFD" at -8..-6, bLength (16) at -5,
         * then the 4-byte CRC.
         */
        fun hasDfuSuffix(file: ByteArray): Boolean {
            val n = file.size
            if (n < DFU_SUFFIX_BYTES) return false
            return file[n - 8] == 'U'.code.toByte() && file[n - 7] == 'F'.code.toByte() &&
                file[n - 6] == 'D'.code.toByte() && file[n - 5].toInt() == DFU_SUFFIX_BYTES
        }

        private fun word(data: ByteArray, offset: Int): Long =
            (data[offset].toLong() and 0xFF) or
                ((data[offset + 1].toLong() and 0xFF) shl 8) or
                ((data[offset + 2].toLong() and 0xFF) shl 16) or
                ((data[offset + 3].toLong() and 0xFF) shl 24)

        private fun hex(value: Long) = String.format(Locale.US, "%08X", value)
    }
}
