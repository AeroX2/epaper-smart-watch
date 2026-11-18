#include "watch_ota.h"

#include <string.h>

#include "watch_ble.h"
#include "watch_crc32.h"
#include "watch_flash.h"
#include "watch_watchdog.h"

namespace {

// OTA header fields (code/EXTERNAL_FLASH.md), little-endian.
constexpr uint32_t HEADER_MAGIC = 0x41544F57UL;  // "WOTA"
constexpr uint32_t HEADER_FORMAT = 1;
constexpr uint32_t HEADER_READY = 0x59444552UL;  // "REDY"
constexpr uint32_t ERASED = 0xFFFFFFFFUL;
constexpr size_t HEADER_CRC_LENGTH = 16;
constexpr size_t HEADER_WRITTEN = 20;  // magic .. header_crc32
constexpr size_t HEADER_READY_OFFSET = 20;
constexpr size_t HEADER_RESULT_OFFSET = 24;
constexpr size_t HEADER_SIZE = 28;
constexpr uint32_t RESULT_INSTALLED = 1;

// Same test as the bootloader's applicationIsValid().
constexpr uint32_t APP_START = 0x08010000UL;
constexpr uint32_t APP_END = 0x080CA000UL;  // exclusive
constexpr uint32_t SRAM_START = 0x20000000UL;
constexpr uint32_t SRAM_END = 0x20030000UL;  // inclusive: initial MSP

bool vectorTableIsValid(uint32_t stack, uint32_t reset) {
  return stack >= SRAM_START && stack <= SRAM_END && reset >= APP_START &&
         reset < APP_END && (reset & 1U) != 0U;
}

}  // namespace

WatchOta::WatchOta(WatchFlash& flash, WatchBle& ble, WatchUiModel& model)
    : flash_(flash), ble_(ble), model_(model) {}

void WatchOta::setIdleHook(void (*hook)()) {
  idle_hook_ = hook;
}

bool WatchOta::supported() const {
#ifdef WATCH_HAS_RESIDENT_DFU
  return flash_.present() &&
         flash_.capacityBytes() >= IMAGE_ADDRESS + MAX_IMAGE_SIZE;
#else
  return false;
#endif
}

bool WatchOta::transferActive() const {
  return state_ == State::Receiving || state_ == State::Verifying ||
         state_ == State::Ready;
}

bool WatchOta::idle() const {
  return state_ == State::Idle && !cleanup_pending_ && !flash_held_;
}

bool WatchOta::screenShown() const {
  return model_.ota_screen != OtaScreen::None;
}

bool WatchOta::eraseHeader() {
  return flash_.eraseSector(HEADER_ADDRESS);
}

void WatchOta::begin(Print& out) {
  state_ = State::Idle;
  report_pending_ = false;
  report_header_kept_ = false;
  if (!flash_.present()) {
    return;
  }
  uint8_t header[HEADER_SIZE];
  if (!flash_.read(HEADER_ADDRESS, header, sizeof(header)) ||
      watchReadLe32(header) != HEADER_MAGIC) {
    return;
  }
  const uint32_t ready = watchReadLe32(header + HEADER_READY_OFFSET);
  const uint32_t result = watchReadLe32(header + HEADER_RESULT_OFFSET);
  if (result != ERASED) {
    // The bootloader will not act on this header again, so it can stay
    // until the result has been reported to the phone.
    report_pending_ = true;
    report_header_kept_ = true;
    report_code_ = result;
    out.print(F("Firmware update result from bootloader: "));
    out.println(result);
    return;
  }
  if (ready == HEADER_READY) {
    // Ready but never attempted: the bootloader predates Bluetooth updates
    // (or was bypassed). Erase at once so that a later reset can never
    // install this stale image; the failure is reported from RAM.
    report_pending_ = true;
    report_code_ = 0;
    out.println(F("[WARN] Staged firmware update was not installed."));
  } else {
    out.println(F("Discarding an interrupted firmware transfer."));
  }
  if (!eraseHeader()) {
    out.println(F("[WARN] Could not erase the update header sector."));
  }
}

void WatchOta::sendBootReport(Print& out) {
  if (!report_pending_) {
    return;
  }
  const bool sent =
      report_code_ == RESULT_INSTALLED
          ? ble_.send("OTA_STATE|INSTALLED|1")
          : ble_.sendf("OTA_STATE|FAILED|%lu",
                       static_cast<unsigned long>(report_code_));
  if (!sent) {
    return;  // try again in the next session
  }
  report_pending_ = false;
  out.print(F("Reported firmware update result "));
  out.println(report_code_);
  if (report_header_kept_) {
    report_header_kept_ = false;
    if (!eraseHeader()) {
      out.println(F("[WARN] Could not erase the update header sector."));
    }
  }
}

void WatchOta::showFailure(const char* reason) {
  model_.ota_screen = OtaScreen::Failed;
  strncpy(model_.ota_detail, reason, sizeof(model_.ota_detail) - 1);
  model_.ota_detail[sizeof(model_.ota_detail) - 1] = '\0';
  ui_dirty_ = true;
}

void WatchOta::discard() {
  const State previous = state_;
  state_ = State::Idle;
  if (previous == State::Receiving || previous == State::Verifying ||
      previous == State::Ready) {
    // A header was written: it must go so nothing can ever install the
    // partial image. Done from service(), never from the BLE context.
    cleanup_pending_ = true;
  }
  if (flash_held_ && !cleanup_pending_) {
    flash_held_ = false;
    flash_.release();
  }
}

void WatchOta::fail(const char* reason) {
  discard();
  ble_.sendf("OTA_STATE|ERROR|%s", reason);
  showFailure(reason);
}

bool WatchOta::handleBegin(uint32_t size, uint32_t crc32, const char* version,
                           bool watch_busy, Print& out) {
  if (!supported()) {
    ble_.send("OTA_STATE|UNSUPPORTED");
    return false;
  }
  if (state_ != State::Idle || cleanup_pending_ || watch_busy) {
    ble_.send("OTA_STATE|BUSY");
    return false;
  }
  if (size < MIN_IMAGE_SIZE || size > MAX_IMAGE_SIZE) {
    ble_.send("OTA_STATE|ERROR|SIZE");
    return false;
  }
  size_ = size;
  crc32_ = crc32;
  received_ = 0;
  deadline_ms_ = millis();
  model_.ota_size = size;
  strncpy(model_.ota_version, version != nullptr ? version : "",
          sizeof(model_.ota_version) - 1);
  model_.ota_version[sizeof(model_.ota_version) - 1] = '\0';
  model_.ota_percent = 0;
  model_.ota_screen = OtaScreen::Confirm;
  state_ = State::Confirming;
  out.print(F("Firmware update offered: "));
  out.print(size);
  out.println(F(" bytes. Select accepts, back declines."));
  return true;
}

void WatchOta::accept(Print& out) {
  if (!ble_.connected()) {
    state_ = State::Idle;
    model_.ota_screen = OtaScreen::None;
    return;
  }
  uint8_t header[HEADER_WRITTEN];
  watchWriteLe32(header, HEADER_MAGIC);
  watchWriteLe32(header + 4, HEADER_FORMAT);
  watchWriteLe32(header + 8, size_);
  watchWriteLe32(header + 12, crc32_);
  watchWriteLe32(header + 16, watchCrc32(header, HEADER_CRC_LENGTH));
  uint8_t check[HEADER_WRITTEN];

  flash_.acquire();
  flash_held_ = true;
  report_header_kept_ = false;  // the old header is erased right here
  const bool ok = eraseHeader() &&
                  flash_.program(HEADER_ADDRESS, header, sizeof(header)) &&
                  flash_.read(HEADER_ADDRESS, check, sizeof(check)) &&
                  memcmp(header, check, sizeof(header)) == 0;
  // From here a header exists, so any exit goes through discard().
  received_ = 0;
  erased_end_ = IMAGE_ADDRESS;
  shown_quarter_ = 0;
  last_reject_offset_ = 0xFFFFFFFFUL;
  deadline_ms_ = millis();
  state_ = State::Receiving;
  if (!ok) {
    out.println(F("[FAIL] Update header could not be written."));
    fail("FLASH");
    return;
  }
  model_.ota_percent = 0;
  model_.ota_screen = OtaScreen::Progress;
  // The trailing 1: full-size packets get through (the radio-core buffer fix in
  // tools/patch_libraries.py). Without it the phone keeps chunks small.
  ble_.sendf("OTA_STATE|ACCEPT|%u|1", static_cast<unsigned>(MAX_CHUNK));
  out.println(F("Firmware update accepted; receiving."));
}

void WatchOta::handleData(const uint8_t* data, size_t length) {
  if (state_ != State::Receiving || data == nullptr || length < 5) {
    return;
  }
  const uint32_t offset = watchReadLe32(data);
  const size_t count = length - 4;
  if (offset != received_ || count > MAX_CHUNK || count > size_ - received_) {
    // Tell the phone where to resume, but not for every stray write.
    const uint32_t now = millis();
    if (last_reject_offset_ != received_ || now - last_reject_ms_ >= 500) {
      last_reject_offset_ = received_;
      last_reject_ms_ = now;
      ble_.sendf("OTA_STATE|PROGRESS|%lu",
                 static_cast<unsigned long>(received_));
    }
    return;
  }

  const uint32_t address = IMAGE_ADDRESS + received_;
  const uint32_t end = address + count;
  // Erase each 4 KiB sector just before the first byte goes into it.
  while (erased_end_ < end) {
    if (!flash_.eraseSector(erased_end_)) {
      fail("FLASH");
      return;
    }
    erased_end_ += WatchFlash::SECTOR_SIZE;
  }
  if (!flash_.program(address, data + 4, count)) {
    fail("FLASH");
    return;
  }

  const uint32_t before = received_;
  received_ += count;
  deadline_ms_ = millis();
  if (received_ / PROGRESS_INTERVAL != before / PROGRESS_INTERVAL ||
      received_ == size_) {
    ble_.sendf("OTA_STATE|PROGRESS|%lu",
               static_cast<unsigned long>(received_));
  }
}

bool WatchOta::handleEnd(Print& out) {
  if (state_ != State::Receiving) {
    if (state_ == State::Idle || state_ == State::Confirming) {
      ble_.send("OTA_STATE|ERROR|SIZE");  // nothing was received
    }
    return false;
  }
  if (received_ != size_) {
    out.println(F("[FAIL] OTA_END before all bytes arrived."));
    fail("SIZE");
    return false;
  }
  state_ = State::Verifying;
  model_.ota_percent = 100;
  model_.ota_screen = OtaScreen::Verifying;
  return true;
}

void WatchOta::verify(Print& out) {
  if (state_ != State::Verifying) {
    return;
  }
  uint8_t buffer[256];
  uint32_t crc = WATCH_CRC32_INIT;
  uint32_t stack = 0;
  uint32_t reset = 0;
  for (uint32_t offset = 0; offset < size_;) {
    const uint32_t remaining = size_ - offset;
    const size_t chunk =
        remaining < sizeof(buffer) ? remaining : sizeof(buffer);
    if (!flash_.read(IMAGE_ADDRESS + offset, buffer, chunk)) {
      out.println(F("[FAIL] Staged image could not be read back."));
      fail("FLASH");
      return;
    }
    if (offset == 0) {
      stack = watchReadLe32(buffer);
      reset = watchReadLe32(buffer + 4);
    }
    crc = watchCrc32Update(crc, buffer, chunk);
    offset += chunk;
    WatchWatchdog::feed();
    if (idle_hook_ != nullptr) {
      idle_hook_();
    }
  }
  if (watchCrc32Finish(crc) != crc32_) {
    out.println(F("[FAIL] Staged image CRC mismatch."));
    fail("CRC");
    return;
  }
  if (!vectorTableIsValid(stack, reset)) {
    out.println(F("[FAIL] Staged image has no valid vector table."));
    fail("IMAGE");
    return;
  }

  // The header must still be exactly what accept() wrote, with ready and
  // result erased, before the single write that arms the bootloader.
  uint8_t header[HEADER_SIZE];
  bool ok = flash_.read(HEADER_ADDRESS, header, sizeof(header)) &&
            watchReadLe32(header) == HEADER_MAGIC &&
            watchReadLe32(header + 4) == HEADER_FORMAT &&
            watchReadLe32(header + 8) == size_ &&
            watchReadLe32(header + 12) == crc32_ &&
            watchReadLe32(header + 16) ==
                watchCrc32(header, HEADER_CRC_LENGTH) &&
            watchReadLe32(header + HEADER_READY_OFFSET) == ERASED &&
            watchReadLe32(header + HEADER_RESULT_OFFSET) == ERASED;
  if (ok) {
    uint8_t ready[4];
    watchWriteLe32(ready, HEADER_READY);
    uint8_t check[4]{};
    ok = flash_.program(HEADER_ADDRESS + HEADER_READY_OFFSET, ready,
                        sizeof(ready)) &&
         flash_.read(HEADER_ADDRESS + HEADER_READY_OFFSET, check,
                     sizeof(check)) &&
         memcmp(ready, check, sizeof(ready)) == 0;
  }
  if (!ok) {
    out.println(F("[FAIL] Update header could not be finalised."));
    fail("FLASH");
    return;
  }
  ble_.send("OTA_STATE|READY");
  ready_ms_ = millis();
  state_ = State::Ready;
  model_.ota_screen = OtaScreen::Installing;
  out.println(F("Firmware update verified; rebooting to install."));
}

bool WatchOta::handleAbort(Print& out) {
  if (state_ == State::Idle) {
    if (model_.ota_screen == OtaScreen::None) {
      return false;
    }
    model_.ota_screen = OtaScreen::None;
    return true;
  }
  out.println(F("Firmware update cancelled by the phone."));
  discard();
  model_.ota_screen = OtaScreen::None;
  return true;
}

bool WatchOta::onDisconnect(Print& out) {
  if (state_ != State::Confirming && state_ != State::Receiving &&
      state_ != State::Verifying) {
    return false;  // Ready: the verified update still installs
  }
  out.println(F("Firmware update dropped: phone disconnected."));
  const bool receiving = state_ != State::Confirming;
  discard();
  if (receiving) {
    showFailure("LINK");
  } else {
    model_.ota_screen = OtaScreen::None;
  }
  ui_dirty_ = false;  // the caller redraws now
  return true;
}

bool WatchOta::handleButton(bool select, bool back, Print& out) {
  switch (model_.ota_screen) {
    case OtaScreen::Confirm:
      if (state_ != State::Confirming) {
        model_.ota_screen = OtaScreen::None;
        return true;
      }
      if (select) {
        accept(out);
        ui_dirty_ = false;  // the caller redraws now
        return true;
      }
      if (back) {
        state_ = State::Idle;
        model_.ota_screen = OtaScreen::None;
        ble_.send("OTA_STATE|DECLINED");
        out.println(F("Firmware update declined."));
        return true;
      }
      return false;
    case OtaScreen::Failed:
      model_.ota_screen = OtaScreen::None;
      return true;
    default:
      return false;  // receiving / verifying / installing: buttons ignored
  }
}

bool WatchOta::service(Print& out) {
  bool redraw = false;
  const uint32_t now = millis();
  if (state_ == State::Confirming &&
      now - deadline_ms_ >= CONFIRM_TIMEOUT_MS) {
    state_ = State::Idle;
    model_.ota_screen = OtaScreen::None;
    ble_.send("OTA_STATE|DECLINED");
    out.println(F("Firmware update not confirmed in time: declined."));
    redraw = true;
  }
  if (state_ == State::Receiving && now - deadline_ms_ >= CHUNK_TIMEOUT_MS) {
    out.println(F("[FAIL] Firmware transfer timed out."));
    fail("TIMEOUT");
  }
  if (cleanup_pending_ && state_ == State::Idle) {
    cleanup_pending_ = false;
    if (!eraseHeader()) {
      out.println(F("[WARN] Could not erase the update header sector."));
    }
    if (flash_held_) {
      flash_held_ = false;
      flash_.release();
    }
  }
  if (state_ == State::Receiving && size_ != 0) {
    const uint8_t quarter =
        static_cast<uint8_t>((static_cast<uint64_t>(received_) * 4U) / size_);
    if (quarter > shown_quarter_ && quarter < 4) {
      shown_quarter_ = quarter;
      model_.ota_percent = static_cast<uint8_t>(quarter * 25U);
      redraw = true;
    }
  }
  if (ui_dirty_) {
    ui_dirty_ = false;
    redraw = true;
  }
  return redraw;
}

bool WatchOta::rebootDue() const {
  return state_ == State::Ready && millis() - ready_ms_ >= REBOOT_DELAY_MS;
}

void WatchOta::printStatus(Print& out) const {
  out.print(F("Bluetooth firmware update: "));
  if (!supported()) {
#ifdef WATCH_HAS_RESIDENT_DFU
    out.println(F("unavailable (external flash not usable)"));
#else
    out.println(F("unsupported in this direct-boot build"));
#endif
    return;
  }
  static const char* const NAMES[] = {"idle", "awaiting confirmation",
                                      "receiving", "verifying", "ready"};
  out.print(NAMES[static_cast<uint8_t>(state_)]);
  if (transferActive()) {
    out.print(F(", "));
    out.print(received_);
    out.print('/');
    out.print(size_);
  }
  if (report_pending_) {
    out.print(F(", result to report: "));
    out.print(report_code_);
  }
  out.println();
}
