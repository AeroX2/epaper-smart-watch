#include "watch_alarm.h"

#include "board_pins.h"
#include "watch_ble.h"
#include "watch_rtc.h"

namespace {

// Notification alert tones. The piezo (LS1, CPT-9019B) is loudest near
// 4 kHz. frequency 0 is a rest; every sequence is well under 600 ms.
struct ToneNote {
  uint16_t frequency;
  uint16_t duration_ms;
};

constexpr uint8_t MAX_NOTES = 5;

struct ToneSequence {
  uint8_t length;
  ToneNote notes[MAX_NOTES];
};

constexpr ToneSequence TONE_SEQUENCES[WatchAlarm::TONE_COUNT] = {
    // 1 Chirp (90 ms)
    {1, {{4000, 90}}},
    // 2 Double (250 ms)
    {3, {{4000, 90}, {0, 70}, {4000, 90}}},
    // 3 Rise (360 ms)
    {3, {{2700, 110}, {3300, 110}, {4000, 140}}},
    // 4 Fall (360 ms)
    {3, {{4000, 110}, {3300, 110}, {2700, 140}}},
    // 5 Triple (330 ms)
    {5, {{4000, 70}, {0, 60}, {4000, 70}, {0, 60}, {4000, 70}}},
};

constexpr uint8_t NO_TONE_SEQUENCE = 0xFF;
constexpr uint32_t NOTIFY_VIBRATION_MS = 180;

}  // namespace

WatchAlarm::WatchAlarm(WatchRtc& rtc, WatchBle& ble, WatchUi& ui,
                       WatchUiModel& model)
    : rtc_(rtc), ble_(ble), ui_(ui), model_(model) {}

void WatchAlarm::begin() {
  timer_last_tick_ = millis();
}

void WatchAlarm::timerChanged() {
  timer_last_tick_ = millis();
}

void WatchAlarm::scheduleSleepAlarm(int32_t delay_seconds, Print& out) {
  if (delay_seconds < 0) {
    out.println(F("Sleep as Android alarm: no delay given; unchanged."));
    return;
  }
  // Keep the deadline inside the signed millis() comparison window.
  constexpr int32_t MAX_DELAY_SECONDS = 2000000;
  const uint32_t delay_ms =
      static_cast<uint32_t>(min(delay_seconds, MAX_DELAY_SECONDS)) * 1000UL;
  sleep_alarm_at_ = millis() + delay_ms;
  if (sleep_alarm_at_ == 0) {
    sleep_alarm_at_ = 1;
  }
  out.println(F("Sleep as Android alarm scheduled."));
}

bool WatchAlarm::stopSleepAlarm(Print& out) {
  sleep_alarm_at_ = 0;
  sleep_alarm_ringing_ = false;
  if (!model_.alarm_ringing) {
    return false;
  }
  dismiss(out);
  return true;
}

void WatchAlarm::stopOutput() {
  analogWrite(BoardPins::VIBRATION, 0);
  noTone(BoardPins::BUZZER);
  actuator_off_at_ = 0;
  tone_sequence_ = NO_TONE_SEQUENCE;
}

void WatchAlarm::startNote() {
  const ToneNote& note = TONE_SEQUENCES[tone_sequence_].notes[tone_note_];
  if (note.frequency != 0) {
    // The duration makes the core's timer end the note by itself, so a
    // stalled main loop can stretch a rest but never a sound.
    tone(BoardPins::BUZZER, note.frequency, note.duration_ms);
  } else {
    noTone(BoardPins::BUZZER);
  }
  tone_note_until_ = millis() + note.duration_ms;
}

void WatchAlarm::notifyAlert(bool vibrate, uint8_t tone_number) {
  if (alerting()) {
    return;  // alarm / find / call keep the actuators
  }
  if (vibrate) {
    analogWrite(BoardPins::VIBRATION, 160);
    actuator_off_at_ = millis() + NOTIFY_VIBRATION_MS;
    if (actuator_off_at_ == 0) {
      actuator_off_at_ = 1;
    }
  }
  if (tone_number >= 1 && tone_number <= TONE_COUNT) {
    tone_sequence_ = static_cast<uint8_t>(tone_number - 1);
    tone_note_ = 0;
    startNote();
  }
}

void WatchAlarm::dismiss(Print& out) {
  if (sleep_alarm_ringing_) {
    ble_.send("SLEEP_DISMISS");
    sleep_alarm_ringing_ = false;
  }
  model_.alarm_ringing = false;
  snooze_until_ = 0;
  stopOutput();
  out.println(F("Alarm dismissed."));
}

void WatchAlarm::snooze(Print& out) {
  if (sleep_alarm_ringing_) {
    ble_.send("SLEEP_SNOOZE");
    sleep_alarm_ringing_ = false;
  }
  model_.alarm_ringing = false;
  snooze_until_ = rtc_.epoch() + 5 * 60;
  stopOutput();
  out.println(F("Alarm snoozed for 5 minutes."));
}

void WatchAlarm::pulseHaptic(uint8_t strength, uint32_t duration_ms) {
  if (alerting()) {
    return;
  }
  analogWrite(BoardPins::VIBRATION, strength);
  actuator_off_at_ = millis() + duration_ms;
}

bool WatchAlarm::needsFastService() const {
  return alerting() || actuator_off_at_ != 0 ||
         tone_sequence_ != NO_TONE_SEQUENCE;
}

bool WatchAlarm::alerting() const {
  return model_.alarm_ringing || call_alert_ || find_alert_;
}

void WatchAlarm::startCallAlert() {
  if (!call_alert_) {
    call_alert_ = true;
    call_alert_started_ = millis();
    alarm_feedback_at_ = call_alert_started_ - 5000;  // first pulse now
  }
}

void WatchAlarm::stopCallAlert() {
  if (call_alert_) {
    call_alert_ = false;
    stopOutput();
  }
}

void WatchAlarm::startFindAlert(Print& out) {
  find_alert_ = true;
  find_alert_started_ = millis();
  alarm_feedback_at_ = find_alert_started_ - 5000;
  out.println(F("Find my watch: alerting."));
}

bool WatchAlarm::stopFindAlert() {
  if (!find_alert_) {
    return false;
  }
  find_alert_ = false;
  stopOutput();
  return true;
}

bool WatchAlarm::findAlertActive() const {
  return find_alert_;
}

bool WatchAlarm::start(const __FlashStringHelper* reason, WatchScreen screen,
                       Print& out) {
  if (model_.alarm_ringing) {
    return false;
  }
  model_.alarm_ringing = true;
  ui_.showScreen(screen);
  alarm_feedback_at_ = 0;
  out.print(F("Alarm started: "));
  out.println(reason);
  return true;
}

void WatchAlarm::serviceOutput() {
  const uint32_t now = millis();
  const bool alert_active = alerting();
  if (tone_sequence_ != NO_TONE_SEQUENCE) {
    if (alert_active) {
      // A higher-priority alert started: it now owns the buzzer.
      tone_sequence_ = NO_TONE_SEQUENCE;
      noTone(BoardPins::BUZZER);
    } else if (static_cast<int32_t>(now - tone_note_until_) >= 0) {
      if (++tone_note_ < TONE_SEQUENCES[tone_sequence_].length) {
        startNote();
      } else {
        tone_sequence_ = NO_TONE_SEQUENCE;
        noTone(BoardPins::BUZZER);
      }
    }
  }
  if (actuator_off_at_ != 0 &&
      static_cast<int32_t>(now - actuator_off_at_) >= 0) {
    if (tone_sequence_ != NO_TONE_SEQUENCE) {
      // Only the notification's vibration pulse ended; the tone goes on.
      analogWrite(BoardPins::VIBRATION, 0);
      actuator_off_at_ = 0;
    } else {
      stopOutput();
    }
  }
  if (find_alert_ && now - find_alert_started_ >= FIND_ALERT_MS) {
    find_alert_ = false;
    stopOutput();
  }
  // If CALL|END never arrives (message dropped, phone app killed) the watch
  // must not ring until someone presses a button.
  if (call_alert_ && now - call_alert_started_ >= CALL_ALERT_MAX_MS) {
    call_alert_ = false;
    stopOutput();
  }

  // One source drives the actuators at a time: alarm, then find, then call.
  bool vibrate = false;
  bool buzz = false;
  uint32_t period_ms = 1400;
  uint16_t frequency = 2200;
  uint32_t on_ms = 220;
  if (model_.alarm_ringing) {
    vibrate = model_.vibration_on;
    buzz = !model_.quiet_mode;
  } else if (find_alert_) {
    vibrate = true;
    buzz = true;
    period_ms = 600;
    frequency = 4000;  // LS1 (CPT-9019B) resonance: loudest for finding
    on_ms = 300;
  } else if (call_alert_) {
    vibrate = model_.vibration_on;
    buzz = true;
    period_ms = 1200;
    frequency = 1800;
    on_ms = 400;
  } else {
    return;
  }
  if (now - alarm_feedback_at_ < period_ms) {
    return;
  }
  alarm_feedback_at_ = now;
  if (vibrate) {
    analogWrite(BoardPins::VIBRATION, 160);
  }
  if (buzz) {
    tone(BoardPins::BUZZER, frequency, on_ms - 40);
  }
  if (vibrate || buzz) {
    actuator_off_at_ = now + on_ms;
  }
}

bool WatchAlarm::service(Print& out) {
  const uint32_t now = millis();
  if (static_cast<int32_t>(now - next_clock_service_at_) < 0) {
    serviceOutput();
    return false;
  }
  next_clock_service_at_ = now + 100;
  rtc_.updateModel(model_);
  bool ui_changed = false;

  if (model_.timer_running) {
    const uint32_t elapsed_seconds = (now - timer_last_tick_) / 1000;
    if (elapsed_seconds > 0) {
      timer_last_tick_ += elapsed_seconds * 1000;
      if (elapsed_seconds >= model_.timer_remaining_seconds) {
        model_.timer_remaining_seconds = 0;
        model_.timer_running = false;
        ui_changed |= start(F("timer complete"), WatchScreen::Timer, out);
      } else {
        model_.timer_remaining_seconds -= elapsed_seconds;
      }
    }
  }

  const time_t current_epoch = rtc_.epoch();
  if (sleep_alarm_at_ != 0 &&
      static_cast<int32_t>(now - sleep_alarm_at_) >= 0) {
    sleep_alarm_at_ = 0;
    sleep_alarm_ringing_ = true;
    ui_changed |= start(F("Sleep as Android"), WatchScreen::Alarms, out);
  }
  if (snooze_until_ != 0 && current_epoch >= snooze_until_) {
    snooze_until_ = 0;
    ui_changed |= start(F("snooze complete"), WatchScreen::Alarms, out);
  }

  const uint32_t minute_key = static_cast<uint32_t>(current_epoch / 60);
  const bool alarm_day =
      (model_.weekday >= 1 && model_.weekday <= 5)
          ? model_.weekday_alarm_on
          : model_.weekend_alarm_on;
  if (alarm_day && model_.hour == model_.alarm_hour &&
      model_.minute == model_.alarm_minute &&
      minute_key != last_alarm_minute_key_) {
    last_alarm_minute_key_ = minute_key;
    ui_changed |= start(F("scheduled alarm"), WatchScreen::Alarms, out);
  }
  serviceOutput();
  return ui_changed;
}
