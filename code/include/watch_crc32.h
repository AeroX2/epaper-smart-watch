#pragma once

#include <stddef.h>
#include <stdint.h>

// CRC-32 IEEE 802.3 (reflected, poly 0xEDB88320, init 0xFFFFFFFF, final XOR
// 0xFFFFFFFF), as required by code/EXTERNAL_FLASH.md. Streaming form: start
// from WATCH_CRC32_INIT, feed chunks with watchCrc32Update(), then finish.
constexpr uint32_t WATCH_CRC32_INIT = 0xFFFFFFFFUL;

inline uint32_t watchCrc32Update(uint32_t state, const uint8_t* data,
                                 size_t length) {
  static const uint32_t NIBBLE_TABLE[16] = {
      0x00000000UL, 0x1DB71064UL, 0x3B6E20C8UL, 0x26D930ACUL,
      0x76DC4190UL, 0x6B6B51F4UL, 0x4DB26158UL, 0x5005713CUL,
      0xEDB88320UL, 0xF00F9344UL, 0xD6D6A3E8UL, 0xCB61B38CUL,
      0x9B64C2B0UL, 0x86D3D2D4UL, 0xA00AE278UL, 0xBDBDF21CUL};
  for (size_t index = 0; index < length; ++index) {
    state ^= data[index];
    state = NIBBLE_TABLE[state & 0x0FU] ^ (state >> 4);
    state = NIBBLE_TABLE[state & 0x0FU] ^ (state >> 4);
  }
  return state;
}

inline uint32_t watchCrc32Finish(uint32_t state) {
  return state ^ 0xFFFFFFFFUL;
}

inline uint32_t watchCrc32(const uint8_t* data, size_t length) {
  return watchCrc32Finish(watchCrc32Update(WATCH_CRC32_INIT, data, length));
}

inline uint32_t watchReadLe32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

inline void watchWriteLe32(uint8_t* p, uint32_t value) {
  p[0] = static_cast<uint8_t>(value);
  p[1] = static_cast<uint8_t>(value >> 8);
  p[2] = static_cast<uint8_t>(value >> 16);
  p[3] = static_cast<uint8_t>(value >> 24);
}
