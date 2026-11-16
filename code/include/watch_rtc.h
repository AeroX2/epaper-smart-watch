#pragma once

#include <Arduino.h>
#include <STM32RTC.h>

#include "watch_ui.h"

// The hardware RTC holds LOCAL wall-clock time. The phone's UTC offset (from
// the protocol TIME message) is kept alongside it, in RAM and in two RTC
// backup registers so it survives ordinary MCU resets.
class WatchRtc {
 public:
  bool begin(Print& out);
  void updateModel(WatchUiModel& model);
  bool setDateTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour,
                   uint8_t minute, uint8_t second);
  bool setEpoch(time_t epoch);
  // Sets local time to utc + offset and remembers the offset.
  bool setUtc(uint32_t utc, int32_t offset_seconds);
  // Local epoch (seconds since 1970 in local wall-clock time).
  time_t epoch();
  int32_t utcOffset() const;
  // Next UTC-offset change (daylight saving) announced by the phone, so the
  // watch can apply it on time even when the phone is asleep or away.
  // transition_utc == 0 clears it. Kept in RAM only.
  void setTransition(uint32_t transition_utc, int32_t offset_after);
  // Applies a due transition; true when the local time just changed.
  bool serviceTransition();
  uint64_t milliseconds();
  // Milliseconds until the displayed minute changes (1..60000).
  uint32_t msUntilNextMinute();
  void restoreAfterBackupReset(time_t saved_epoch, uint32_t elapsed_ms,
                               Print& out);
  void print(Print& out);
  bool usingLse() const;
  // False while the clock only holds its build-time seed: nothing (phone
  // TIME or the console) has set it since the backup domain last lost power.
  // Date-keyed data (step history) must not be trusted to it.
  bool timeTrusted() const;

 private:
  static time_t compileEpoch();
  void storeOffset();

  STM32RTC& rtc_ = STM32RTC::getInstance();
  bool ready_ = false;
  bool lse_ready_ = false;
  bool time_trusted_ = false;
  int32_t utc_offset_ = 0;
  uint32_t transition_utc_ = 0;
  int32_t transition_offset_ = 0;
};
