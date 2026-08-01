#pragma once

#include <Arduino.h>

#include "watch_ui.h"

class WatchBle;
class WatchRtc;

class WatchAlarm {
 public:
  WatchAlarm(WatchRtc& rtc, WatchBle& ble, WatchUi& ui, WatchUiModel& model);

  void begin();
  void timerChanged();
  bool service(Print& out);

  void scheduleSleepAlarm(uint32_t delay_ms, Print& out);
  bool stopSleepAlarm(Print& out);
  void dismiss(Print& out);
  void snooze(Print& out);
  void pulseHaptic(uint8_t strength, uint32_t duration_ms);
  bool needsFastService() const;

 private:
  bool start(const __FlashStringHelper* reason, WatchScreen screen, Print& out);
  void stopOutput();
  void serviceOutput();

  WatchRtc& rtc_;
  WatchBle& ble_;
  WatchUi& ui_;
  WatchUiModel& model_;

  uint32_t timer_last_tick_ = 0;
  uint32_t sleep_alarm_at_ = 0;
  uint32_t alarm_feedback_at_ = 0;
  uint32_t actuator_off_at_ = 0;
  uint32_t last_alarm_minute_key_ = 0;
  uint32_t next_clock_service_at_ = 0;
  time_t snooze_until_ = 0;
  bool sleep_alarm_ringing_ = false;
};
