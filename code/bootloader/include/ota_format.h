/*
 * Staged-update (OTA) header format and checks shared by the bootloader and
 * its host-side test. Implements the bootloader half of code/EXTERNAL_FLASH.md.
 *
 * Pure logic only: no hardware access, no dependencies beyond <stdint.h>, so
 * the same code can be compiled for the PC.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ota {

/* External flash map (EXTERNAL_FLASH.md "Map"). */
constexpr uint32_t kHeaderAddress = 0x100000UL;
constexpr uint32_t kImageAddress = 0x101000UL;
constexpr uint32_t kMinImageSize = 8UL;
constexpr uint32_t kMaxImageSize = 761856UL; /* 186 x 4 KiB */

/* Internal application slot. */
constexpr uint32_t kAppStart = 0x08010000UL;
constexpr uint32_t kAppEnd = 0x080CA000UL; /* exclusive */
constexpr uint32_t kSramStart = 0x20000000UL;
constexpr uint32_t kSramEnd = 0x20030000UL; /* inclusive: initial MSP */

/* OTA header (EXTERNAL_FLASH.md "OTA header"), little-endian fields. */
constexpr uint32_t kHeaderSize = 28UL;
constexpr uint32_t kHeaderCrcLength = 16UL;
constexpr uint32_t kResultOffset = 24UL;
constexpr uint32_t kMagic = 0x41544F57UL; /* "WOTA" */
constexpr uint32_t kFormat = 1UL;
constexpr uint32_t kReady = 0x59444552UL; /* "REDY" */
constexpr uint32_t kErased = 0xFFFFFFFFUL;

constexpr uint32_t kResultInstalled = 1UL;
constexpr uint32_t kResultStagedCrc = 2UL;
constexpr uint32_t kResultBadImage = 3UL;
constexpr uint32_t kResultProgramFailed = 4UL;

static_assert(kMaxImageSize == kAppEnd - kAppStart,
              "staged image area must match the application slot");

struct Header {
  uint32_t image_size;
  uint32_t image_crc32;
};

/* CRC-32 IEEE 802.3: reflected, poly 0xEDB88320, init 0xFFFFFFFF, final XOR
 * 0xFFFFFFFF. Streaming form: start from kCrcInit, feed chunks with
 * crc32Update(), finish with crc32Finish(). */
constexpr uint32_t kCrcInit = 0xFFFFFFFFUL;

inline uint32_t crc32Update(uint32_t state, const uint8_t* data,
                            size_t length) {
  static const uint32_t kNibbleTable[16] = {
      0x00000000UL, 0x1DB71064UL, 0x3B6E20C8UL, 0x26D930ACUL,
      0x76DC4190UL, 0x6B6B51F4UL, 0x4DB26158UL, 0x5005713CUL,
      0xEDB88320UL, 0xF00F9344UL, 0xD6D6A3E8UL, 0xCB61B38CUL,
      0x9B64C2B0UL, 0x86D3D2D4UL, 0xA00AE278UL, 0xBDBDF21CUL};
  for (size_t i = 0; i < length; ++i) {
    state ^= data[i];
    state = kNibbleTable[state & 0x0FU] ^ (state >> 4);
    state = kNibbleTable[state & 0x0FU] ^ (state >> 4);
  }
  return state;
}

inline uint32_t crc32Finish(uint32_t state) { return state ^ 0xFFFFFFFFUL; }

inline uint32_t crc32(const uint8_t* data, size_t length) {
  return crc32Finish(crc32Update(kCrcInit, data, length));
}

inline uint32_t readLe32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

/* True only when the raw header sector asks for an install: magic, format and
 * header CRC right, ready == "REDY", result still erased, size in range. */
inline bool headerRequestsInstall(const uint8_t raw[kHeaderSize],
                                  Header* out) {
  if (readLe32(raw + 0) != kMagic || readLe32(raw + 4) != kFormat) {
    return false;
  }
  if (readLe32(raw + 16) != crc32(raw, kHeaderCrcLength)) {
    return false;
  }
  if (readLe32(raw + 20) != kReady || readLe32(raw + kResultOffset) != kErased) {
    return false;
  }
  const uint32_t size = readLe32(raw + 8);
  if (size < kMinImageSize || size > kMaxImageSize) {
    return false;
  }
  out->image_size = size;
  out->image_crc32 = readLe32(raw + 12);
  return true;
}

/* Same test applicationIsValid() applies to the installed application:
 * initial stack in SRAM, reset vector inside the application slot, thumb bit
 * set. */
inline bool vectorTableIsValid(uint32_t stack, uint32_t reset) {
  return stack >= kSramStart && stack <= kSramEnd && reset >= kAppStart &&
         reset < kAppEnd && (reset & 1U) != 0U;
}

}  // namespace ota
