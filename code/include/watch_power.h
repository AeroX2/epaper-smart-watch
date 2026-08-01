#pragma once

#include <Arduino.h>

class WatchRtc;

enum WatchWakeEvent : uint8_t {
  WATCH_WAKE_NONE = 0,
  WATCH_WAKE_BUTTON = 1U << 0,
  WATCH_WAKE_ACCELEROMETER = 1U << 1,
  WATCH_WAKE_BATTERY = 1U << 2,
};

class WatchPower {
 public:
  void begin(WatchRtc& rtc, Print& out);
  uint8_t takeEvents();

  // Stops the M4 in ordinary Sleep mode until an interrupt or RTC deadline.
  // The main clock/regulator stay active for native USB CDC reliability;
  // SysTick is suspended and elapsed time is restored from the hardware RTC.
  void sleepFor(uint32_t maximum_ms);

 private:
  static void onButtonInterrupt();
  static void onAccelerometerInterrupt();
  static void onBatteryInterrupt();
  static void setEvent(uint8_t event);

  static WatchPower* instance_;
  volatile uint8_t pending_events_ = WATCH_WAKE_NONE;
  WatchRtc* rtc_ = nullptr;
};
