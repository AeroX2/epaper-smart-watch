#include "watch_alarm.h"

#include "board_pins.h"
#include "watch_ble.h"
#include "watch_rtc.h"

WatchAlarm::WatchAlarm(WatchRtc& rtc, WatchBle& ble, WatchUi& ui,
                       WatchUiModel& model)
    : rtc_(rtc), ble_(ble), ui_(ui), model_(model) {}

void WatchAlarm::begin() {
  timer_last_tick_ = millis();
}

void WatchAlarm::timerChanged() {
  timer_last_tick_ = millis();
}

void WatchAlarm::scheduleSleepAlarm(uint32_t delay_ms, Print& out) {
  if (delay_ms == 0) {
    sleep_alarm_at_ = millis();
  } else if (delay_ms != UINT32_MAX) {
    sleep_alarm_at_ = millis() + delay_ms;
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
}

void WatchAlarm::dismiss(Print& out) {
  if (sleep_alarm_ringing_) {
    ble_.sendCommand("SLEEP,DISMISS");
    sleep_alarm_ringing_ = false;
  }
  model_.alarm_ringing = false;
  snooze_until_ = 0;
  stopOutput();
  out.println(F("Alarm dismissed."));
}

void WatchAlarm::snooze(Print& out) {
  if (sleep_alarm_ringing_) {
    ble_.sendCommand("SLEEP,SNOOZE");
    sleep_alarm_ringing_ = false;
  }
  model_.alarm_ringing = false;
  snooze_until_ = rtc_.epoch() + 5 * 60;
  stopOutput();
  out.println(F("Alarm snoozed for 5 minutes."));
}

void WatchAlarm::pulseHaptic(uint8_t strength, uint32_t duration_ms) {
  if (model_.alarm_ringing) {
    return;
  }
  analogWrite(BoardPins::VIBRATION, strength);
  actuator_off_at_ = millis() + duration_ms;
}

bool WatchAlarm::needsFastService() const {
  return model_.alarm_ringing || actuator_off_at_ != 0;
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
  if (actuator_off_at_ != 0 &&
      static_cast<int32_t>(now - actuator_off_at_) >= 0) {
    stopOutput();
  }
  if (!model_.alarm_ringing || now - alarm_feedback_at_ < 1400) {
    return;
  }
  alarm_feedback_at_ = now;
  if (model_.vibration_on) {
    analogWrite(BoardPins::VIBRATION, 160);
  }
  if (!model_.quiet_mode) {
    tone(BoardPins::BUZZER, 2200, 180);
  }
  if (model_.vibration_on || !model_.quiet_mode) {
    actuator_off_at_ = now + 220;
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
