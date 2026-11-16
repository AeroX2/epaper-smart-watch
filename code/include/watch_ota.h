#pragma once

#include <Arduino.h>

#include "watch_ui.h"

class WatchBle;
class WatchFlash;

// Receives a firmware image over Bluetooth and stages it in the external
// flash for the resident bootloader (PROTOCOL.md "Firmware update over
// Bluetooth", code/EXTERNAL_FLASH.md). Nothing here touches the internal
// flash: a failed or interrupted transfer leaves the running firmware alone.
//
// Context rules: handleData() runs inside BLE.poll() (possibly from the
// display wait hook, i.e. in the middle of an e-paper refresh). It only
// writes the external flash, updates counters and queues messages; it never
// renders. Everything else runs from the main loop.
class WatchOta {
 public:
  static constexpr uint32_t HEADER_ADDRESS = 0x100000UL;
  static constexpr uint32_t IMAGE_ADDRESS = 0x101000UL;
  static constexpr uint32_t MIN_IMAGE_SIZE = 8;
  static constexpr uint32_t MAX_IMAGE_SIZE = 761856UL;
  static constexpr uint16_t MAX_CHUNK = 240;

  WatchOta(WatchFlash& flash, WatchBle& ble, WatchUiModel& model);

  // Reads the header left by a previous update (boot-time report).
  void begin(Print& out);
  // Called regularly during the long read-back verification.
  void setIdleHook(void (*hook)());

  // Resident-bootloader build with a working, large enough flash.
  bool supported() const;
  // Receiving, verifying or waiting to reboot: STATUS/HISTORY are suspended.
  bool transferActive() const;
  // Nothing in progress: no prompt, transfer, verification or clean-up.
  bool idle() const;
  // The update overlay owns the screen and the buttons.
  bool screenShown() const;

  // Phone messages. Each returns true when the screen must be redrawn.
  bool handleBegin(uint32_t size, uint32_t crc32, const char* version,
                   bool watch_busy, Print& out);
  // True when the transfer is complete and verify() must be called (after
  // the caller has drawn the "verifying" screen).
  bool handleEnd(Print& out);
  // Reads the staged copy back; on success writes the header's ready word,
  // queues OTA_STATE|READY and arms the reboot. Always redraw afterwards.
  void verify(Print& out);
  bool handleAbort(Print& out);

  // OTA data characteristic write: offset (4, LE) + payload.
  void handleData(const uint8_t* data, size_t length);

  // Button while screenShown(). Returns true when the screen changed.
  bool handleButton(bool select, bool back, Print& out);
  // Timeouts, deferred flash clean-up, progress steps. True = redraw.
  bool service(Print& out);
  // Returns true when the screen changed.
  bool onDisconnect(Print& out);
  // Sends OTA_STATE|INSTALLED / FAILED once in the first session after an
  // update, then erases the header sector.
  void sendBootReport(Print& out);

  // Verified and READY sent at least two seconds ago: reset now.
  bool rebootDue() const;

  void printStatus(Print& out) const;

 private:
  enum class State : uint8_t {
    Idle,
    Confirming,
    Receiving,
    Verifying,
    Ready,
  };

  static constexpr uint32_t CONFIRM_TIMEOUT_MS = 60000;
  static constexpr uint32_t CHUNK_TIMEOUT_MS = 30000;
  static constexpr uint32_t PROGRESS_INTERVAL = 16384;
  static constexpr uint32_t REBOOT_DELAY_MS = 2000;

  void accept(Print& out);
  // Ends the transfer and reports OTA_STATE|ERROR|reason. Safe from the BLE
  // context: the header erase is deferred to service().
  void fail(const char* reason);
  void discard();
  void showFailure(const char* reason);
  bool eraseHeader();

  WatchFlash& flash_;
  WatchBle& ble_;
  WatchUiModel& model_;
  void (*idle_hook_)() = nullptr;

  volatile State state_ = State::Idle;
  uint32_t size_ = 0;
  uint32_t crc32_ = 0;
  uint32_t received_ = 0;
  uint32_t erased_end_ = 0;      // flash address up to which sectors are erased
  uint32_t deadline_ms_ = 0;     // confirm start / last accepted chunk
  uint32_t ready_ms_ = 0;
  uint32_t last_reject_ms_ = 0;
  uint32_t last_reject_offset_ = 0xFFFFFFFFUL;
  uint8_t shown_quarter_ = 0;
  bool flash_held_ = false;
  bool cleanup_pending_ = false;  // header sector must be erased
  bool ui_dirty_ = false;

  bool report_pending_ = false;
  bool report_header_kept_ = false;
  uint32_t report_code_ = 0;
};
