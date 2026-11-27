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

  void dismiss(Print& out);
  void snooze(Print& out);
  bool needsFastService() const;
  // Milliseconds until the next thing service() has to act on: timer expiry,
  // snooze end, today's scheduled alarm, or local
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

  // A vibration pattern: steps of (motor drive 0-255, milliseconds), ended
  // by a step of 0 ms.
  struct HapticStep {
    uint8_t strength;
    uint16_t ms;
  };

  // Notification alert: a double buzz and/or alert tone 1-5
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
  // Rings over whatever is on screen (WatchUi draws the ringing screen).
  bool start(const __FlashStringHelper* reason, bool timer, Print& out);
  void stopOutput();
  void startNote();
  void startHaptic(const HapticStep* pattern);
  void hapticOff();

  WatchRtc& rtc_;
  WatchBle& ble_;
  WatchUi& ui_;
  WatchUiModel& model_;

  uint32_t timer_last_tick_ = 0;
  // The running timer is redrawn every TIMER_REDRAW_SECONDS while its
  // screen is showing; an e-paper update is too slow to do every second.
  static constexpr uint32_t TIMER_REDRAW_SECONDS = 3;
  uint32_t timer_drawn_step_ = 0;
  uint32_t alarm_feedback_at_ = 0;
  uint32_t actuator_off_at_ = 0;
  uint32_t last_alarm_minute_key_ = 0;
  uint32_t next_clock_service_at_ = 0;
  time_t snooze_until_ = 0;
  bool call_alert_ = false;
  bool find_alert_ = false;
  uint32_t find_alert_started_ = 0;
  uint32_t call_alert_started_ = 0;

  // Notification tone being played: index into the note table, 0xFF = none.
  uint8_t tone_sequence_ = 0xFF;
  uint8_t tone_note_ = 0;
  uint32_t tone_note_until_ = 0;

  // Vibration pattern being played (nullptr = none), runs alongside a tone.
  const HapticStep* haptic_ = nullptr;
  uint32_t haptic_step_until_ = 0;

  static constexpr uint32_t FIND_ALERT_MS = 10000;
  static constexpr uint32_t CALL_ALERT_MAX_MS = 60000;
  bool alerting() const;
};
