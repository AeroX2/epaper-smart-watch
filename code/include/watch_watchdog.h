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
  void printStatus(Print& out) const;

 private:
  bool reset_by_watchdog_ = false;
};
