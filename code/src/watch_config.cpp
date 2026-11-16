#include "watch_config.h"

#include <string.h>

#include "watch_crc32.h"
#include "watch_flash.h"

namespace {

constexpr uint32_t RECORD_MAGIC = 0x47464357UL;  // "WCFG"
constexpr uint8_t RECORD_VERSION = 1;

constexpr uint8_t KIND_SETTINGS = 1;
constexpr uint8_t KIND_BOND = 2;
constexpr uint8_t KIND_STEPS = 3;

constexpr size_t SETTINGS_LENGTH = 4;
constexpr size_t BOND_LENGTH = 24;
constexpr size_t STEPS_LENGTH = 60;

}  // namespace

void WatchConfigStore::begin(WatchFlash& flash, Print& out) {
  flash_ = &flash;
  failed_ = false;
  settings_cached_ = false;
  for (uint8_t kind = 1; kind <= KIND_COUNT; ++kind) {
    slots_[kind - 1] = Slot{};
  }
  if (!flash.present()) {
    out.println(F("Config store: no external flash, running from RAM."));
    return;
  }
  flash.acquire();
  for (uint8_t kind = 1; kind <= KIND_COUNT; ++kind) {
    scan(kind);
  }
  flash.release();
  printStatus(out);
}

bool WatchConfigStore::available() const {
  return flash_ != nullptr && flash_->present() && !failed_;
}

uint32_t WatchConfigStore::sectorAddress(uint8_t kind, uint8_t copy) {
  return (static_cast<uint32_t>(kind - 1) * 2U + copy) *
         WatchFlash::SECTOR_SIZE;
}

bool WatchConfigStore::readCopy(uint8_t kind, uint8_t copy, uint8_t* payload,
                                size_t max_length, uint16_t& length,
                                uint32_t& sequence) {
  uint8_t record[MAX_RECORD];
  const uint32_t address = sectorAddress(kind, copy);
  if (!flash_->read(address, record, HEADER_SIZE)) {
    return false;
  }
  const uint16_t stored_length =
      static_cast<uint16_t>(record[6] | (record[7] << 8));
  if (watchReadLe32(record) != RECORD_MAGIC || record[4] != kind ||
      record[5] != RECORD_VERSION || stored_length == 0 ||
      stored_length > MAX_PAYLOAD) {
    return false;
  }
  if (!flash_->read(address + HEADER_SIZE, record + HEADER_SIZE,
                    stored_length + 4U)) {
    return false;
  }
  const size_t body = HEADER_SIZE + stored_length;
  if (watchReadLe32(record + body) != watchCrc32(record, body)) {
    return false;
  }
  length = stored_length;
  sequence = watchReadLe32(record + 8);
  if (payload != nullptr) {
    if (stored_length > max_length) {
      return false;
    }
    memcpy(payload, record + HEADER_SIZE, stored_length);
  }
  return true;
}

void WatchConfigStore::scan(uint8_t kind) {
  Slot& slot = slots_[kind - 1];
  slot = Slot{};
  for (uint8_t copy = 0; copy < 2; ++copy) {
    uint16_t length = 0;
    uint32_t sequence = 0;
    if (!readCopy(kind, copy, nullptr, 0, length, sequence)) {
      continue;
    }
    if (!slot.valid || sequence > slot.sequence) {
      slot.valid = true;
      slot.active = copy;
      slot.sequence = sequence;
      slot.length = length;
    }
  }
}

bool WatchConfigStore::load(uint8_t kind, uint8_t* payload, size_t length) {
  if (flash_ == nullptr || !flash_->present()) {
    return false;
  }
  const Slot& slot = slots_[kind - 1];
  if (!slot.valid || slot.length != length) {
    return false;
  }
  uint16_t stored_length = 0;
  uint32_t sequence = 0;
  return readCopy(kind, slot.active, payload, length, stored_length,
                  sequence) &&
         stored_length == length;
}

bool WatchConfigStore::save(uint8_t kind, const uint8_t* payload,
                            size_t length) {
  if (!available() || length == 0 || length > MAX_PAYLOAD) {
    return false;
  }
  Slot& slot = slots_[kind - 1];
  const uint8_t target = slot.valid ? static_cast<uint8_t>(1 - slot.active) : 0;
  const uint32_t sequence = slot.valid ? slot.sequence + 1 : 1;

  uint8_t record[MAX_RECORD];
  watchWriteLe32(record, RECORD_MAGIC);
  record[4] = kind;
  record[5] = RECORD_VERSION;
  record[6] = static_cast<uint8_t>(length);
  record[7] = static_cast<uint8_t>(length >> 8);
  watchWriteLe32(record + 8, sequence);
  memcpy(record + HEADER_SIZE, payload, length);
  const size_t body = HEADER_SIZE + length;
  watchWriteLe32(record + body, watchCrc32(record, body));
  const size_t total = body + 4;

  const uint32_t address = sectorAddress(kind, target);
  uint8_t check[MAX_RECORD];
  flash_->acquire();
  bool ok = flash_->eraseSector(address) &&
            flash_->program(address, record, total) &&
            flash_->read(address, check, total) &&
            memcmp(record, check, total) == 0;
  flash_->release();
  if (write_count_ < UINT16_MAX) {
    ++write_count_;
  }
  if (!ok) {
    // The previous copy is untouched. Stop writing: a flash that fails once
    // is not trusted again until the next boot.
    failed_ = true;
    return false;
  }
  slot.valid = true;
  slot.active = target;
  slot.sequence = sequence;
  slot.length = static_cast<uint16_t>(length);
  return true;
}

bool WatchConfigStore::loadSettings(StoredSettings& settings) {
  uint8_t payload[SETTINGS_LENGTH];
  if (!load(KIND_SETTINGS, payload, sizeof(payload))) {
    return false;
  }
  settings.vibration = payload[0] != 0;
  settings.quiet = payload[1] != 0;
  settings.tone = payload[2] <= 5 ? payload[2] : 0;
  settings.secure_link = payload[3] == 1;
  settings_cache_ = settings;
  settings_cached_ = true;
  return true;
}

bool WatchConfigStore::saveSettings(const StoredSettings& settings) {
  if (settings_cached_ && settings_cache_.vibration == settings.vibration &&
      settings_cache_.quiet == settings.quiet &&
      settings_cache_.tone == settings.tone &&
      settings_cache_.secure_link == settings.secure_link) {
    return true;  // unchanged: no flash wear
  }
  const uint8_t payload[SETTINGS_LENGTH] = {
      static_cast<uint8_t>(settings.vibration ? 1 : 0),
      static_cast<uint8_t>(settings.quiet ? 1 : 0),
      settings.tone,
      static_cast<uint8_t>(settings.secure_link ? 1 : 0),
  };
  if (!save(KIND_SETTINGS, payload, sizeof(payload))) {
    return false;
  }
  settings_cache_ = settings;
  settings_cached_ = true;
  return true;
}

bool WatchConfigStore::loadBond(StoredBond& bond) {
  uint8_t payload[BOND_LENGTH];
  if (!load(KIND_BOND, payload, sizeof(payload))) {
    return false;
  }
  bond.valid = payload[0] == 1;
  memcpy(bond.address, payload + 2, sizeof(bond.address));
  memcpy(bond.ltk, payload + 8, sizeof(bond.ltk));
  return true;
}

bool WatchConfigStore::saveBond(const StoredBond& bond) {
  uint8_t payload[BOND_LENGTH]{};
  payload[0] = bond.valid ? 1 : 0;
  if (bond.valid) {
    memcpy(payload + 2, bond.address, sizeof(bond.address));
    memcpy(payload + 8, bond.ltk, sizeof(bond.ltk));
  }
  return save(KIND_BOND, payload, sizeof(payload));
}

bool WatchConfigStore::eraseBond() {
  const Slot before = slots_[KIND_BOND - 1];
  if (!before.valid) {
    return true;  // nothing was ever stored
  }
  if (!saveBond(StoredBond{})) {
    return false;
  }
  // The empty record now lives in the other sector; wipe the one that still
  // holds the old key so it cannot be read back out of the chip.
  flash_->acquire();
  const bool ok =
      flash_->eraseSector(sectorAddress(KIND_BOND, before.active));
  flash_->release();
  if (!ok) {
    failed_ = true;
  }
  return ok;
}

bool WatchConfigStore::loadSteps(StoredSteps& steps) {
  uint8_t payload[STEPS_LENGTH];
  if (!load(KIND_STEPS, payload, sizeof(payload))) {
    return false;
  }
  steps.today_day = watchReadLe32(payload);
  steps.today_steps = watchReadLe32(payload + 4);
  steps.count = payload[8] <= StoredSteps::MAX_DAYS ? payload[8] : 0;
  for (uint8_t index = 0; index < StoredSteps::MAX_DAYS; ++index) {
    steps.history[index].day = watchReadLe32(payload + 12 + index * 8);
    steps.history[index].steps = watchReadLe32(payload + 16 + index * 8);
  }
  return true;
}

bool WatchConfigStore::saveSteps(const StoredSteps& steps) {
  uint8_t payload[STEPS_LENGTH]{};
  watchWriteLe32(payload, steps.today_day);
  watchWriteLe32(payload + 4, steps.today_steps);
  payload[8] = steps.count <= StoredSteps::MAX_DAYS ? steps.count
                                                    : StoredSteps::MAX_DAYS;
  for (uint8_t index = 0; index < payload[8]; ++index) {
    watchWriteLe32(payload + 12 + index * 8, steps.history[index].day);
    watchWriteLe32(payload + 16 + index * 8, steps.history[index].steps);
  }
  return save(KIND_STEPS, payload, sizeof(payload));
}

void WatchConfigStore::printStatus(Print& out) {
  static const char* const NAMES[KIND_COUNT] = {"settings", "bond", "steps"};
  out.print(F("Config store: "));
  if (flash_ == nullptr || !flash_->present()) {
    out.println(F("no external flash (RAM only)"));
    return;
  }
  out.print(failed_ ? F("WRITE FAILED, now RAM only;") : F("ok;"));
  for (uint8_t kind = 0; kind < KIND_COUNT; ++kind) {
    const Slot& slot = slots_[kind];
    out.print(' ');
    out.print(NAMES[kind]);
    if (slot.valid) {
      out.print(slot.active == 0 ? F("=A#") : F("=B#"));
      out.print(slot.sequence);
    } else {
      out.print(F("=none"));
    }
  }
  out.print(F("; writes this boot: "));
  out.println(write_count_);
}
