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
class WatchWatchdog {
 public:
  // Longest time any single blocking call may run without a feed. BLE.begin()
  // can legitimately take ~10 s and cannot be fed from inside, so this leaves
  // a margin above that.
  static constexpr uint32_t TIMEOUT_MS = 20000;

  // Records whether the previous reset came from the IWDG, then starts it.
  void begin();
  static void feed();

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
  uint32_t hang_pc_ = 0;
  uint32_t hang_lr_ = 0;
};
