#pragma once

#include <Arduino.h>

// Driver for U4, the XT25F32F 4 MB serial flash on the QUADSPI pins
// (CLK PA3, NCS PD3, IO0 PD4, IO1 PD5, WP# PD6, HOLD# PD7).
//
// Follows the access rules in code/EXTERNAL_FLASH.md: single-line SPI
// commands only (0x9F, 0x05, 0x06, 0x03, 0x02, 0x20, 0xB9, 0xAB), WP#/HOLD#
// driven high as plain GPIO, a timeout on every wait, and deep power-down
// whenever no caller holds the chip awake.
//
// The bus is bit-banged (SPI mode 0, MSB first) rather than driven by the
// QUADSPI peripheral: nothing here is speed-critical, the GPIO path has no
// clock-tree, FIFO or alternate-function dependencies that cannot be checked
// without hardware, and a missing chip can only ever produce a wrong ID or a
// timeout, never a hang.
//
// Not re-entrant, and it never polls BLE or the display: callers must not
// start one flash operation from inside another.
class WatchFlash {
 public:
  static constexpr uint32_t CAPACITY = 0x400000UL;
  static constexpr uint32_t SECTOR_SIZE = 4096;
  static constexpr uint32_t PAGE_SIZE = 256;
  static constexpr uint32_t EXPECTED_JEDEC_ID = 0x0B4016UL;  // XTX XT25F32F

  // Configures every pin itself (the bootloader hands them over as analog),
  // releases the chip from power-down, probes it and puts it back to sleep.
  void begin();

  // Re-reads the JEDEC ID (non-destructive). Updates present().
  bool probe();
  bool present() const { return present_; }
  uint32_t jedecId() const { return jedec_id_; }
  // Capacity decoded from the JEDEC ID, 0 if it cannot be decoded.
  uint32_t capacityBytes() const;

  // Keeps the chip out of deep power-down across several operations. Calls
  // nest; every acquire() needs one release().
  void acquire();
  void release();

  // All three fail (false) when the chip is absent, the range is outside the
  // device, or the chip stays busy past its timeout. Each returns only after
  // the chip's busy flag has cleared.
  bool read(uint32_t address, uint8_t* data, size_t length);
  // Splits at 256-byte page boundaries. The target must already be erased.
  bool program(uint32_t address, const uint8_t* data, size_t length);
  // Erases the 4 KiB sector containing address.
  bool eraseSector(uint32_t address);

  // Wakes the chip (leaving it awake, in standby) and waits, bounded, for any
  // erase/program to finish. Used immediately before a deliberate reset so
  // the bootloader never meets a busy or sleeping chip.
  bool prepareForReset(uint32_t timeout_ms);

  // Status register 1, or 0xFF if the chip is absent.
  uint8_t statusRegister();
  void printStatus(Print& out);

 private:
  static constexpr uint32_t PROGRAM_TIMEOUT_MS = 100;
  static constexpr uint32_t ERASE_TIMEOUT_MS = 2000;
  static constexpr uint32_t BOOT_BUSY_TIMEOUT_MS = 500;

  void select();
  void deselect();
  void sendByte(uint8_t value);
  uint8_t receiveByte();
  void sendAddress(uint8_t command, uint32_t address);
  void wake();
  void sleep();
  uint8_t readStatusRaw();
  uint32_t readIdRaw();
  bool writeEnable();
  bool waitReady(uint32_t timeout_ms);
  bool rangeValid(uint32_t address, size_t length) const;

  bool configured_ = false;
  bool present_ = false;
  uint32_t jedec_id_ = 0xFFFFFFUL;
  uint8_t hold_count_ = 0;
};
