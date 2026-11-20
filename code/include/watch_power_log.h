#pragma once

#include <Arduino.h>

class WatchFlash;

// One power log record: 64 bytes, little-endian (the MCU's own order),
// written as-is to external flash. See code/EXTERNAL_FLASH.md.
struct __attribute__((packed)) PowerLogRecord {
  static constexpr uint8_t TYPE_BOOT = 1;      // written once at startup
  static constexpr uint8_t TYPE_INTERVAL = 2;  // one per logging interval

  // flags
  static constexpr uint8_t FLAG_RADIO_ON = 1U << 0;
  static constexpr uint8_t FLAG_CONNECTED = 1U << 1;
  static constexpr uint8_t FLAG_USB_POWER = 1U << 2;
  static constexpr uint8_t FLAG_LIGHT_ON = 1U << 3;
  static constexpr uint8_t FLAG_ADV_FAST = 1U << 4;
  static constexpr uint8_t FLAG_BATTERY_VALID = 1U << 5;
  static constexpr uint8_t FLAG_WATCHDOG_RESET = 1U << 6;  // boot records
  static constexpr uint8_t FLAG_TIME_VALID = 1U << 7;

  uint32_t seq;          // 1, 2, 3... (0 and 0xFFFFFFFF are never written)
  uint32_t local_epoch;  // RTC local time when written
  uint32_t uptime_s;
  uint16_t interval_s;   // length of the interval this record covers
  uint8_t type;
  uint8_t flags;         // radio / link / USB / light state when written
  uint32_t stop_ms;      // time in Stop 2 during the interval
                         // (boot records: PC of a recorded hang, 0 = none)
  uint32_t idle_ms;      // time in ordinary Sleep (boot: LR of that hang)
  uint32_t awake_ms;     // the rest: running (boot: watchdog resets in a row)
  uint16_t sleeps;       // sleep() calls (Stop + Sleep entries)
  uint16_t stops;        // of which Stop entries
  uint16_t wake[7];      // Stop wakes by cause: RTC, button, accelerometer,
                         // battery, radio, USB, other
  uint16_t battery_mv;
  uint8_t battery_pct;
  uint8_t busy_seen;     // WatchSleepBusy bits seen (boot: deep sleep mode)
  uint16_t blocks_seen;  // WatchSleepBlock bits seen
  uint16_t renders;      // display refreshes (boot: stage the previous
                         // start-up stopped at, 0 = it finished)
  uint16_t display_cs;   // time spent refreshing, centiseconds
  uint16_t ble_rx;       // writes from the phone
  uint16_t ble_tx;       // notifications to the phone
  uint32_t crc;          // CRC-32 of the 60 bytes above
};
static_assert(sizeof(PowerLogRecord) == 64, "PowerLogRecord must be 64 bytes");

// Battery-use log: a ring of PowerLogRecords in the top 2 MiB of the
// external flash, about 22 days at one record a minute. The oldest sector is
// erased when the ring wraps. Records are found again after a reset by their
// sequence numbers, so nothing else is stored.
class WatchPowerLog {
 public:
  static constexpr uint32_t REGION_START = 0x200000UL;
  static constexpr uint32_t REGION_SIZE = 0x200000UL;
  static constexpr uint32_t RECORD_SIZE = sizeof(PowerLogRecord);
  static constexpr uint32_t SECTORS = REGION_SIZE / 4096UL;
  static constexpr uint32_t PER_SECTOR = 4096UL / RECORD_SIZE;
  static constexpr uint32_t SLOTS = SECTORS * PER_SECTOR;

  // Finds the newest record. Without a working flash the log is disabled.
  void begin(WatchFlash& flash, Print& out);
  bool available() const { return flash_ != nullptr; }

  // Fills seq and crc, then writes the record. False if it could not.
  bool append(PowerLogRecord& record);

  // Every valid record, oldest first, as CSV between "POWERLOG BEGIN" and
  // "POWERLOG END" lines. Feeds the watchdog as it goes.
  void dump(Print& out);
  // Erases every sector that holds records.
  void clear(Print& out);
  void printStatus(Print& out) const;

 private:
  static uint32_t slotAddress(uint32_t slot) {
    return REGION_START + slot * RECORD_SIZE;
  }
  bool readRecord(uint32_t slot, PowerLogRecord& record);
  static bool valid(const PowerLogRecord& record);

  WatchFlash* flash_ = nullptr;
  uint32_t next_slot_ = 0;  // where the next record goes
  uint32_t next_seq_ = 1;
  uint32_t records_ = 0;    // valid records found at begin + appended
  uint32_t write_failures_ = 0;
};
