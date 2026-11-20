#pragma once

#include <Arduino.h>

class WatchFlash;

// Power-fail-safe configuration store in the external flash range
// 0x000000-0x00FFFF (private to the application, see code/EXTERNAL_FLASH.md).
//
// Each record kind owns two 4 KiB sectors ("A" and "B"). A save erases the
// sector that does NOT hold the newest valid copy, writes the new copy there
// with the next sequence number, and reads it back. Power loss at any point
// therefore leaves the previous copy intact; load picks the valid copy with
// the highest sequence number.
//
//   sector pair          kind
//   0x000000 / 0x001000  1  settings
//   0x002000 / 0x003000  2  bond key
//   0x004000 / 0x005000  3  step history
//   0x006000 / 0x007000  4  power policy
//   0x008000 / 0x009000  5  button layout
//   0x00A000 - 0x00FFFF  unused
//
// Record, little-endian, at the start of its sector:
//   0  u32 magic      0x47464357 ("WCFG")
//   4  u8  kind
//   5  u8  version    1
//   6  u16 length     payload bytes
//   8  u32 sequence
//  12  payload[length]
//  12+length  u32 crc32 (IEEE 802.3) over bytes 0 .. 12+length-1
//
// Payloads (version 1):
//   settings (4):  u8 vibration, u8 quiet, u8 tone (0-5), u8 secure_link
//   bond (24):     u8 valid, u8 reserved, u8 address[6], u8 ltk[16]
//   steps (60):    u32 today_day, u32 today_steps, u8 count, u8 reserved[3],
//                  6 x { u32 day, u32 steps }   (oldest first; day = local
//                  epoch / 86400)
//   power (4):     u8 deep_sleep_mode (0 off, 1 radio-off only, 2 always),
//                  u8 radio_on (1 = Bluetooth was on; restore it at boot),
//                  u8 reserved[2]
//   buttons (5):   u8 format (3), then the role of PCB buttons 1-4 (BTN_1..4,
//                  i.e. SW2, SW3, SW4, SW5): 0 Home, 1 Next, 2 Previous,
//                  3 Select, each once. Earlier records listed corners under
//                  wrong names (format 2, and the 4-byte first format); they
//                  are converted on load.
//
// When the flash is absent, or after a write fails, every save returns false
// and the callers simply keep running from RAM.

struct StoredSettings {
  bool vibration = true;
  bool quiet = false;
  uint8_t tone = 0;
  bool secure_link = false;
};

struct StoredBond {
  bool valid = false;
  uint8_t address[6]{};
  uint8_t ltk[16]{};
};

struct StoredSteps {
  static constexpr uint8_t MAX_DAYS = 6;
  struct Day {
    uint32_t day;
    uint32_t steps;
  };
  uint32_t today_day = 0;
  uint32_t today_steps = 0;
  uint8_t count = 0;
  Day history[MAX_DAYS]{};
};

class WatchConfigStore {
 public:
  void begin(WatchFlash& flash, Print& out);
  // Flash present and no write has failed since boot.
  bool available() const;

  bool loadSettings(StoredSettings& settings);
  // Writes only when the values differ from the stored copy.
  bool saveSettings(const StoredSettings& settings);

  bool loadBond(StoredBond& bond);
  bool saveBond(const StoredBond& bond);
  // Stores an empty bond and erases the sector that held the old key.
  bool eraseBond();

  bool loadSteps(StoredSteps& steps);
  bool saveSteps(const StoredSteps& steps);

  // Deep-sleep mode (a DeepSleepMode value). No record = default.
  bool loadPowerMode(uint8_t& mode);
  bool savePowerMode(uint8_t mode);
  // Whether Bluetooth was last switched on. No record = off.
  bool loadRadioOn(bool& on);
  bool saveRadioOn(bool on);

  // Button roles by corner (see WatchButtons). No record = default layout.
  bool loadButtons(uint8_t roles[4]);
  bool saveButtons(const uint8_t roles[4]);

  void printStatus(Print& out);

 private:
  static constexpr uint8_t KIND_COUNT = 5;
  static constexpr size_t MAX_PAYLOAD = 60;
  static constexpr size_t HEADER_SIZE = 12;
  static constexpr size_t MAX_RECORD = HEADER_SIZE + MAX_PAYLOAD + 4;

  struct Slot {
    bool valid = false;       // a good copy exists in flash
    uint8_t active = 0;       // which sector (0 = A, 1 = B) holds it
    uint32_t sequence = 0;
    uint16_t length = 0;
  };

  static uint32_t sectorAddress(uint8_t kind, uint8_t copy);
  bool readCopy(uint8_t kind, uint8_t copy, uint8_t* payload,
                size_t max_length, uint16_t& length, uint32_t& sequence);
  void scan(uint8_t kind);
  bool load(uint8_t kind, uint8_t* payload, size_t length);
  bool save(uint8_t kind, const uint8_t* payload, size_t length);

  WatchFlash* flash_ = nullptr;
  bool failed_ = false;
  uint16_t write_count_ = 0;
  Slot slots_[KIND_COUNT]{};
  bool settings_cached_ = false;
  StoredSettings settings_cache_{};
};
