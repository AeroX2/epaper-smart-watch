#pragma once

#include <Arduino.h>
#include <STM32RTC.h>

#include "watch_ui.h"

class WatchRtc {
 public:
  bool begin(Print& out);
  void updateModel(WatchUiModel& model);
  bool setDateTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour,
                   uint8_t minute, uint8_t second);
  bool setEpoch(time_t epoch);
  time_t epoch();
  uint64_t milliseconds();
  void restoreAfterBackupReset(time_t saved_epoch, uint32_t elapsed_ms,
                               Print& out);
  void print(Print& out);
  bool usingLse() const;

 private:
  static time_t compileEpoch();

  STM32RTC& rtc_ = STM32RTC::getInstance();
  bool ready_ = false;
  bool lse_ready_ = false;
};
