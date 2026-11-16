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

  // delay_seconds < 0 leaves any existing schedule untouched.
  void scheduleSleepAlarm(int32_t delay_seconds, Print& out);
  bool stopSleepAlarm(Print& out);
  void dismiss(Print& out);
  void snooze(Print& out);
  void pulseHaptic(uint8_t strength, uint32_t duration_ms);
  bool needsFastService() const;
  // Milliseconds until the next thing service() has to act on: timer expiry,
  // Sleep as Android alarm, snooze end, today's scheduled alarm, or local
  // midnight (when "today" and its alarm are re-evaluated). UINT32_MAX if
  // nothing is scheduled.
  uint32_t msUntilNextEvent(uint32_t now);

  // Actuator "off" states that draw nothing: the motor gate as a plain GPIO
  // low with its PWM timer released, and the piezo gate low with the tone
  // timer paused (noTone() alone can leave the gate high mid-cycle, holding
  // the piezo's bleed resistor across the supply).
  static void vibrationOff();
  static void buzzerOff();

  // Incoming-call ring pattern (caller decides whether quiet mode allows it).
  void startCallAlert();
  void stopCallAlert();
  // Find-my-watch: vibrate and buzz regardless of settings for ~10 s.
  void startFindAlert(Print& out);
  bool stopFindAlert();
  bool findAlertActive() const;

  // Notification alert: one short vibration pulse and/or alert tone 1-5
  // (0 = no tone). The caller applies the vibration/quiet settings. Ignored
  // while an alarm, find-my-watch or call alert owns the actuators, and cut
  // short if one of those starts.
  static constexpr uint8_t TONE_COUNT = 5;
  void notifyAlert(bool vibrate, uint8_t tone);
  // Advances the actuator timing only (pulse ends, tone notes, alert
  // patterns). Safe to call from the display wait hook: it never renders,
  // touches BLE or changes alarm state.
  void serviceOutput();

 private:
  bool start(const __FlashStringHelper* reason, WatchScreen screen, Print& out);
  void stopOutput();
  void startNote();

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
  bool call_alert_ = false;
  bool find_alert_ = false;
  uint32_t find_alert_started_ = 0;
  uint32_t call_alert_started_ = 0;

  // Notification tone being played: index into the note table, 0xFF = none.
  uint8_t tone_sequence_ = 0xFF;
  uint8_t tone_note_ = 0;
  uint32_t tone_note_until_ = 0;

  static constexpr uint32_t FIND_ALERT_MS = 10000;
  static constexpr uint32_t CALL_ALERT_MAX_MS = 60000;
  bool alerting() const;
};
