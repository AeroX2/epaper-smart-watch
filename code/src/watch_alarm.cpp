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

// Vibration patterns. The motor is a small ERM: from rest it takes a few
// tens of ms to spin up, so each buzz starts with a short full-drive kick
// and then holds at a lower drive. Two or more buzzes with a gap are far
// easier to feel than one: the change is what the wrist notices. The
// holds stay below full drive and the kicks short because the 130 mAh cell
// is rated 65 mA continuous.
using Step = WatchAlarm::HapticStep;
constexpr Step NOTIFY_HAPTIC[] = {
    {255, 40}, {200, 150}, {0, 120}, {255, 40}, {200, 150}, {0, 0}};
constexpr Step ALARM_HAPTIC[] = {
    {255, 40}, {220, 220}, {0, 130}, {255, 40}, {220, 220}, {0, 0}};
constexpr Step CALL_HAPTIC[] = {{255, 40}, {220, 420}, {0, 0}};
constexpr Step FIND_HAPTIC[] = {{255, 40}, {230, 260}, {0, 0}};

}  // namespace

WatchAlarm::WatchAlarm(WatchRtc& rtc, WatchBle& ble, WatchUi& ui,
                       WatchUiModel& model)
    : rtc_(rtc), ble_(ble), ui_(ui), model_(model) {}

void WatchAlarm::begin() {
  timer_last_tick_ = millis();
}

void WatchAlarm::timerChanged() {
  timer_last_tick_ = millis();
  timer_drawn_step_ = (model_.timer_remaining_seconds +
                       TIMER_REDRAW_SECONDS - 1) / TIMER_REDRAW_SECONDS;
}

void WatchAlarm::vibrationOff() {
  digitalWrite(BoardPins::VIBRATION, LOW);
  pinMode(BoardPins::VIBRATION, OUTPUT);  // also stops the PWM timer
}

void WatchAlarm::buzzerOff() {
  noTone(BoardPins::BUZZER);
  digitalWrite(BoardPins::BUZZER, LOW);
}

uint32_t WatchAlarm::msUntilNextEvent(uint32_t now) {
  uint32_t best = UINT32_MAX;
  const auto consider = [&best](uint32_t ms) {
    if (ms < best) {
      best = ms;
    }
  };
  if (model_.timer_running) {
    const uint64_t total =
        static_cast<uint64_t>(model_.timer_remaining_seconds) * 1000ULL;
    const uint32_t elapsed = now - timer_last_tick_;
    consider(elapsed >= total ? 0 : static_cast<uint32_t>(total - elapsed));
    if (ui_.screen() == WatchScreen::Timer) {
      // Wake for the next redraw of the countdown.
      uint32_t to_step = model_.timer_remaining_seconds % TIMER_REDRAW_SECONDS;
      if (to_step == 0) {
        to_step = TIMER_REDRAW_SECONDS;
      }
      consider(elapsed >= to_step * 1000UL ? 0 : to_step * 1000UL - elapsed);
    }
  }
  const time_t epoch = rtc_.epoch();
  if (snooze_until_ != 0) {
    consider(snooze_until_ <= epoch
                 ? 0
                 : static_cast<uint32_t>(snooze_until_ - epoch) * 1000UL);
  }
  if (model_.time_valid && epoch > 0) {
    const uint32_t second_of_day = static_cast<uint32_t>(epoch % 86400);
    consider((86400UL - second_of_day) * 1000UL);
    const bool alarm_day =
        (model_.weekday >= 1 && model_.weekday <= 5)
            ? model_.weekday_alarm_on
            : model_.weekend_alarm_on;
    const uint32_t alarm_second =
        model_.alarm_hour * 3600UL + model_.alarm_minute * 60UL;
    if (alarm_day && second_of_day < alarm_second) {
      consider((alarm_second - second_of_day) * 1000UL);
    }
  }
  return best;
}

void WatchAlarm::startHaptic(const HapticStep* pattern) {
  haptic_ = pattern;
  analogWrite(BoardPins::VIBRATION, pattern->strength);
  haptic_step_until_ = millis() + pattern->ms;
}

void WatchAlarm::hapticOff() {
  haptic_ = nullptr;
  vibrationOff();
}

void WatchAlarm::stopOutput() {
  hapticOff();
  buzzerOff();
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
    buzzerOff();
  }
  tone_note_until_ = millis() + note.duration_ms;
}

void WatchAlarm::notifyAlert(bool vibrate, uint8_t tone_number) {
  if (alerting()) {
    return;  // alarm / find / call keep the actuators
  }
  if (vibrate) {
    startHaptic(NOTIFY_HAPTIC);
  }
  if (tone_number >= 1 && tone_number <= TONE_COUNT) {
    tone_sequence_ = static_cast<uint8_t>(tone_number - 1);
    tone_note_ = 0;
    startNote();
  }
}

void WatchAlarm::dismiss(Print& out) {
  model_.alarm_ringing = false;
  model_.timer_ringing = false;
  snooze_until_ = 0;
  stopOutput();
  out.println(F("Alarm dismissed."));
}

void WatchAlarm::snooze(Print& out) {
  if (model_.timer_ringing) {
    dismiss(out);  // a finished timer has nothing to snooze
    return;
  }
  model_.alarm_ringing = false;
  snooze_until_ = rtc_.epoch() + 5 * 60;
  stopOutput();
  out.println(F("Alarm snoozed for 5 minutes."));
}

bool WatchAlarm::needsFastService() const {
  return alerting() || actuator_off_at_ != 0 || haptic_ != nullptr ||
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

bool WatchAlarm::start(const __FlashStringHelper* reason, bool timer,
                       Print& out) {
  if (model_.alarm_ringing) {
    return false;
  }
  model_.alarm_ringing = true;
  model_.timer_ringing = timer;
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
      buzzerOff();
    } else if (static_cast<int32_t>(now - tone_note_until_) >= 0) {
      if (++tone_note_ < TONE_SEQUENCES[tone_sequence_].length) {
        startNote();
      } else {
        tone_sequence_ = NO_TONE_SEQUENCE;
        buzzerOff();
      }
    }
  }
  if (haptic_ != nullptr &&
      static_cast<int32_t>(now - haptic_step_until_) >= 0) {
    ++haptic_;
    if (haptic_->ms == 0) {
      hapticOff();
    } else {
      analogWrite(BoardPins::VIBRATION, haptic_->strength);
      haptic_step_until_ = now + haptic_->ms;
    }
  }
  // An alert's beep ended (the vibration pattern ends by itself).
  if (actuator_off_at_ != 0 &&
      static_cast<int32_t>(now - actuator_off_at_) >= 0) {
    actuator_off_at_ = 0;
    if (tone_sequence_ == NO_TONE_SEQUENCE) {
      buzzerOff();
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
  const HapticStep* haptic = ALARM_HAPTIC;
  if (model_.alarm_ringing) {
    vibrate = model_.vibration_on;
    buzz = !model_.quiet_mode;
  } else if (find_alert_) {
    vibrate = true;
    buzz = true;
    period_ms = 600;
    frequency = 4000;  // LS1 (CPT-9019B) resonance: loudest for finding
    on_ms = 300;
    haptic = FIND_HAPTIC;
  } else if (call_alert_) {
    vibrate = model_.vibration_on;
    buzz = true;
    period_ms = 1200;
    frequency = 1800;
    on_ms = 400;
    haptic = CALL_HAPTIC;
  } else {
    return;
  }
  if (now - alarm_feedback_at_ < period_ms) {
    return;
  }
  alarm_feedback_at_ = now;
  if (vibrate) {
    startHaptic(haptic);
  }
  if (buzz) {
    tone(BoardPins::BUZZER, frequency, on_ms - 40);
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
        ui_changed |= start(F("timer complete"), true, out);
      } else {
        model_.timer_remaining_seconds -= elapsed_seconds;
        const uint32_t step = (model_.timer_remaining_seconds +
                               TIMER_REDRAW_SECONDS - 1) /
                              TIMER_REDRAW_SECONDS;
        if (step != timer_drawn_step_) {
          timer_drawn_step_ = step;
          ui_changed |= ui_.screen() == WatchScreen::Timer;
        }
      }
    }
  }

  const time_t current_epoch = rtc_.epoch();
  if (snooze_until_ != 0 && current_epoch >= snooze_until_) {
    snooze_until_ = 0;
    ui_changed |= start(F("snooze complete"), false, out);
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
    ui_changed |= start(F("scheduled alarm"), false, out);
  }
  serviceOutput();
  return ui_changed;
}
