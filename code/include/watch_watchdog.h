#pragma once

#include <Arduino.h>

// Independent watchdog (IWDG) supervision. Some vendor code, notably
// STM32duinoBLE's CPU2 handshake, spins forever without a timeout; the IWDG
// turns those hangs into a reset instead of a dead watch that needs its
// battery disconnected.
//
// The IWDG runs from the LSI and keeps counting in Sleep mode. Once started it
// can only be stopped by a reset, which also leaves the resident bootloader
// and its DFU mode unsupervised.
// Number of times the hang monitor has broken an I2C interrupt storm (see
// watch_watchdog.cpp). The bus is unusable after one until it is restarted.
uint32_t watchI2cStormCount();

class WatchWatchdog {
 public:
  // Longest time any single blocking call may run without a feed. BLE.begin()
  // can legitimately take ~10 s and cannot be fed from inside, so this leaves
  // a margin above that.
  static constexpr uint32_t TIMEOUT_MS = 20000;

  // A firmware that hangs on every boot never reaches its console, so it
  // cannot be asked to enter USB DFU. After this many watchdog resets in a
  // row, begin() requests the resident bootloader's DFU mode instead of
  // starting the application again.
  static constexpr uint8_t CRASH_LOOP_RESETS = 3;
  // The run of watchdog resets ends once the firmware has stayed up this long.
  static constexpr uint32_t HEALTHY_AFTER_MS = 60000;

  // Records whether the previous reset came from the IWDG, then starts it.
  // Does not return when it sends a crash-looping watch to USB DFU.
  void begin();
  static void feed();
  // Start-up breadcrumbs: setup() records each stage it reaches in a backup
  // register (BOOT_STAGE_DONE at the end). If a boot hangs, the next one
  // reports the last stage reached (lastBootStage(), 0 = finished/unknown).
  static constexpr uint8_t BOOT_STAGE_DONE = 99;
  static void bootStage(uint8_t stage);
  // millis() when bootStage(stage) was called, 0 if it was not (1-15).
  static uint32_t bootStageMs(uint8_t stage);
  uint8_t lastBootStage() const { return last_boot_stage_; }
  // Call from the main loop; clears the crash-loop count once healthy.
  void service();
  uint8_t watchdogResetsInARow() const { return resets_in_a_row_; }

  bool resetByWatchdog() const { return reset_by_watchdog_; }
  // Where the firmware was stuck before the last watchdog reset, if the hang
  // monitor caught it (0 = nothing recorded).
  uint32_t hangPc() const { return hang_pc_; }
  uint32_t hangLr() const { return hang_lr_; }
  void printStatus(Print& out) const;

 private:
  // Hang monitor: a slow LPTIM1 tick that notices when feed() has not been
  // called for HANG_RECORD_MS and stores the interrupted program counter and
  // link register in RTC backup registers, a few seconds before the IWDG
  // resets the chip. The next boot prints them. LPTIM1 runs from PCLK1, so it
  // stops in Stop mode and never wakes the watch from it.
  static constexpr uint32_t HANG_RECORD_MS = 14000;
  void startHangMonitor();

  bool reset_by_watchdog_ = false;
  uint8_t resets_in_a_row_ = 0;
  bool healthy_ = false;
  uint32_t hang_pc_ = 0;
  uint32_t hang_lr_ = 0;
  uint8_t last_boot_stage_ = 0;
  uint32_t stale_hang_pc_ = 0;  // the registers as found, magic or not
  uint32_t stale_hang_lr_ = 0;
};
