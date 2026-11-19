#include "watch_power_log.h"

#include "watch_crc32.h"
#include "watch_flash.h"
#include "watch_watchdog.h"

namespace {

constexpr uint32_t ERASED = 0xFFFFFFFFUL;

uint32_t recordCrc(const PowerLogRecord& record) {
  return watchCrc32(reinterpret_cast<const uint8_t*>(&record),
                    sizeof(record) - sizeof(record.crc));
}

}  // namespace

bool WatchPowerLog::valid(const PowerLogRecord& record) {
  return record.seq != 0 && record.seq != ERASED &&
         record.crc == recordCrc(record);
}

bool WatchPowerLog::readRecord(uint32_t slot, PowerLogRecord& record) {
  return flash_->read(slotAddress(slot), reinterpret_cast<uint8_t*>(&record),
                      sizeof(record)) &&
         valid(record);
}

void WatchPowerLog::begin(WatchFlash& flash, Print& out) {
  flash_ = nullptr;
  if (!flash.present()) {
    out.println(F("Power log: no external flash; not logging."));
    return;
  }
  flash_ = &flash;
  flash.acquire();
  // Records are written in order, so each sector's first record has the
  // lowest sequence number in it: the newest sector is the one whose first
  // record has the highest. Then find the end of that sector.
  uint32_t best_seq = 0;
  uint32_t best_sector = 0;
  uint32_t used_sectors = 0;
  for (uint32_t sector = 0; sector < SECTORS; ++sector) {
    uint32_t seq = ERASED;
    flash.read(slotAddress(sector * PER_SECTOR),
               reinterpret_cast<uint8_t*>(&seq), sizeof(seq));
    if (seq != ERASED && seq != 0) {
      ++used_sectors;
      if (seq > best_seq) {
        best_seq = seq;
        best_sector = sector;
      }
    }
    if ((sector & 63U) == 0) {
      WatchWatchdog::feed();
    }
  }
  if (best_seq == 0) {
    next_slot_ = 0;
    next_seq_ = 1;
    records_ = 0;
  } else {
    uint32_t last_slot = best_sector * PER_SECTOR;
    uint32_t last_seq = best_seq;
    PowerLogRecord record{};
    for (uint32_t i = 1; i < PER_SECTOR; ++i) {
      const uint32_t slot = best_sector * PER_SECTOR + i;
      if (!flash.read(slotAddress(slot), reinterpret_cast<uint8_t*>(&record),
                      sizeof(record)) ||
          record.seq == ERASED) {
        break;
      }
      if (valid(record) && record.seq > last_seq) {
        last_seq = record.seq;
      }
      last_slot = slot;
    }
    next_slot_ = (last_slot + 1) % SLOTS;
    next_seq_ = last_seq + 1;
    // Full sectors before this one, plus this one's records (an estimate:
    // records with a bad CRC are counted too).
    records_ = (used_sectors - 1) * PER_SECTOR +
               (last_slot - best_sector * PER_SECTOR + 1);
  }
  flash.release();
  printStatus(out);
}

bool WatchPowerLog::append(PowerLogRecord& record) {
  if (flash_ == nullptr) {
    return false;
  }
  record.seq = next_seq_;
  record.crc = recordCrc(record);
  const uint32_t address = slotAddress(next_slot_);
  flash_->acquire();
  bool ok = true;
  if (next_slot_ % PER_SECTOR == 0) {
    // Starting a sector: it may hold the oldest records of a full ring.
    ok = flash_->eraseSector(address);
    if (ok && records_ >= SLOTS - PER_SECTOR) {
      records_ -= PER_SECTOR;
    }
  }
  ok = ok && flash_->program(address, reinterpret_cast<const uint8_t*>(&record),
                             sizeof(record));
  flash_->release();
  if (!ok) {
    // Skip the slot: a half-written record fails its CRC and is ignored.
    ++write_failures_;
  } else {
    ++records_;
    ++next_seq_;
  }
  next_slot_ = (next_slot_ + 1) % SLOTS;
  return ok;
}

void WatchPowerLog::dump(Print& out) {
  out.println(F("POWERLOG BEGIN"));
  out.println(F(
      "seq,local_epoch,uptime_s,interval_s,type,flags,stop_ms,idle_ms,"
      "awake_ms,sleeps,stops,wake_rtc,wake_button,wake_accel,wake_battery,"
      "wake_radio,wake_usb,wake_other,battery_mv,battery_pct,busy_seen,"
      "blocks_seen,renders,display_cs,ble_rx,ble_tx"));
  if (flash_ == nullptr) {
    out.println(F("POWERLOG END 0"));
    return;
  }
  flash_->acquire();
  uint32_t printed = 0;
  PowerLogRecord r{};
  // Oldest first, going round once. The sector the next record goes into
  // holds the newest records (mid-sector) or the oldest ones (at a sector
  // boundary, before it is erased).
  const uint32_t next_sector = next_slot_ / PER_SECTOR;
  const uint32_t first_sector = next_slot_ % PER_SECTOR == 0
                                    ? next_sector
                                    : (next_sector + 1) % SECTORS;
  for (uint32_t s = 0; s < SECTORS; ++s) {
    const uint32_t sector = (first_sector + s) % SECTORS;
    WatchWatchdog::feed();
    for (uint32_t i = 0; i < PER_SECTOR; ++i) {
      const uint32_t slot = sector * PER_SECTOR + i;
      if (!flash_->read(slotAddress(slot), reinterpret_cast<uint8_t*>(&r),
                        sizeof(r)) ||
          r.seq == ERASED) {
        break;  // the rest of this sector is empty
      }
      if (!valid(r)) {
        continue;
      }
      char line[200];
      snprintf(line, sizeof(line),
               "%lu,%lu,%lu,%u,%u,%u,%lu,%lu,%lu,%u,%u,%u,%u,%u,%u,%u,%u,%u,"
               "%u,%u,%u,%u,%u,%u,%u,%u",
               static_cast<unsigned long>(r.seq),
               static_cast<unsigned long>(r.local_epoch),
               static_cast<unsigned long>(r.uptime_s), r.interval_s, r.type,
               r.flags, static_cast<unsigned long>(r.stop_ms),
               static_cast<unsigned long>(r.idle_ms),
               static_cast<unsigned long>(r.awake_ms), r.sleeps, r.stops,
               r.wake[0], r.wake[1], r.wake[2], r.wake[3], r.wake[4], r.wake[5],
               r.wake[6], r.battery_mv, r.battery_pct, r.busy_seen,
               r.blocks_seen, r.renders, r.display_cs, r.ble_rx, r.ble_tx);
      out.println(line);
      ++printed;
    }
  }
  flash_->release();
  out.print(F("POWERLOG END "));
  out.println(printed);
}

void WatchPowerLog::clear(Print& out) {
  if (flash_ == nullptr) {
    return;
  }
  flash_->acquire();
  uint32_t erased = 0;
  for (uint32_t sector = 0; sector < SECTORS; ++sector) {
    WatchWatchdog::feed();
    uint32_t seq = ERASED;
    flash_->read(slotAddress(sector * PER_SECTOR),
                 reinterpret_cast<uint8_t*>(&seq), sizeof(seq));
    if (seq != ERASED && flash_->eraseSector(slotAddress(sector * PER_SECTOR))) {
      ++erased;
    }
  }
  flash_->release();
  next_slot_ = 0;
  next_seq_ = 1;
  records_ = 0;
  out.print(F("Power log cleared ("));
  out.print(erased);
  out.println(F(" sectors)."));
}

void WatchPowerLog::printStatus(Print& out) const {
  out.print(F("Power log: "));
  if (flash_ == nullptr) {
    out.println(F("off (no external flash)"));
    return;
  }
  out.print(records_);
  out.print(F(" records of "));
  out.print(SLOTS);
  out.print(F(", next #"));
  out.print(next_seq_);
  if (write_failures_ != 0) {
    out.print(F(", write failures "));
    out.print(write_failures_);
  }
  out.println();
}
