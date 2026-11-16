#include "watch_activity.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "watch_ble.h"
#include "watch_config.h"
#include "watch_diagnostics.h"
#include "watch_rtc.h"
#include "watch_ui.h"

WatchActivity::WatchActivity(Bma400& accelerometer,
                             WatchDiagnostics& diagnostics, WatchRtc& rtc,
                             WatchBle& ble, WatchUiModel& model,
                             WatchConfigStore& store)
    : accelerometer_(accelerometer),
      diagnostics_(diagnostics),
      rtc_(rtc),
      ble_(ble),
      model_(model),
      store_(store) {}

void WatchActivity::restore(Print& out) {
  last_save_ms_ = millis();
  StoredSteps stored{};
  if (!store_.loadSteps(stored)) {
    return;
  }
  history_count_ = 0;
  uint32_t previous_day = 0;
  for (uint8_t index = 0; index < stored.count && index < HISTORY_DAYS;
       ++index) {
    // Keep only a strictly ascending list of plausible days.
    if (stored.history[index].day > previous_day) {
      history_[history_count_++] = {stored.history[index].day,
                                    stored.history[index].steps};
      previous_day = stored.history[index].day;
    }
  }
  pending_valid_ = stored.today_day != 0;
  pending_day_ = stored.today_day;
  pending_steps_ = stored.today_steps;
  out.print(F("Step history restored from flash: "));
  out.print(history_count_);
  out.println(F(" completed day(s)."));
}

void WatchActivity::begin(Print& out) {
  restore(out);
  if (!diagnostics_.accelerometerReady()) {
    out.println(F("[WARN] BMA400 interrupt activity tracking unavailable."));
    return;
  }
  hardware_activity_ready_ = accelerometer_.configureActivityInterrupts();
  uint8_t activity_type = 0;
  if (hardware_activity_ready_ &&
      accelerometer_.readStepCount(sensor_step_baseline_, activity_type)) {
    out.println(F("[PASS] BMA400 hardware step/data-ready interrupts armed."));
  } else {
    hardware_activity_ready_ = false;
    out.println(F("[WARN] BMA400 interrupt configuration failed."));
  }
}

void WatchActivity::startSleepTracking(Print& out) {
  sleep_tracking_ = true;
  sleep_accel_seeded_ = false;
  sleep_max_delta_mg_ = 0;
  sleep_interval_started_at_ = millis();
  if (hardware_activity_ready_ &&
      !accelerometer_.enableDataReadyInterrupt(true)) {
    out.println(F("[WARN] Could not enable BMA400 sleep data-ready IRQ."));
  }
  out.println(F("Sleep as Android movement tracking started."));
}

void WatchActivity::stopSleepTracking(Print& out) {
  sleep_tracking_ = false;
  sleep_pause_until_epoch_ = 0;
  sleep_max_delta_mg_ = 0;
  if (hardware_activity_ready_) {
    accelerometer_.enableDataReadyInterrupt(false);
  }
  out.println(F("Sleep as Android movement tracking stopped."));
}

void WatchActivity::pauseSleepTracking(int32_t seconds, Print& out) {
  if (seconds <= 0) {
    sleep_pause_until_epoch_ = 0;
    out.println(F("Sleep tracking resumed."));
    return;
  }
  const uint64_t until =
      static_cast<uint64_t>(rtc_.epoch()) + static_cast<uint64_t>(seconds);
  sleep_pause_until_epoch_ =
      until > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(until);
  out.print(F("Sleep tracking paused for seconds: "));
  out.println(seconds);
}

namespace {

// Days since 1970-01-01 -> YYYYMMDD (Howard Hinnant's civil_from_days).
uint32_t yyyymmddFromDay(uint32_t day_index) {
  const int32_t z = static_cast<int32_t>(day_index) + 719468;
  const int32_t era = z / 146097;
  const uint32_t doe = static_cast<uint32_t>(z - era * 146097);
  const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const uint32_t mp = (5 * doy + 2) / 153;
  const uint32_t day = doy - (153 * mp + 2) / 5 + 1;
  const uint32_t month = mp < 10 ? mp + 3 : mp - 9;
  const uint32_t year = yoe + static_cast<uint32_t>(era) * 400 +
                        (month <= 2 ? 1 : 0);
  return year * 10000 + month * 100 + day;
}

}  // namespace

void WatchActivity::dropHistoryFrom(uint32_t day) {
  uint8_t kept = 0;
  for (uint8_t index = 0; index < history_count_; ++index) {
    if (history_[index].local_day < day) {
      history_[kept++] = history_[index];
    }
  }
  history_count_ = kept;
}

void WatchActivity::archive(uint32_t day, uint32_t steps) {
  // Completed days stay strictly ascending even after the clock moves back.
  dropHistoryFrom(day);
  if (history_count_ == HISTORY_DAYS) {
    memmove(history_, history_ + 1, sizeof(StepDay) * (HISTORY_DAYS - 1));
    --history_count_;
  }
  history_[history_count_++] = {day, steps};
}

void WatchActivity::serviceDay(uint32_t local_day, bool time_trusted,
                               Print& out) {
  if (!time_trusted || local_day == 0) {
    return;
  }
  bool changed = false;
  if (pending_valid_) {
    // First look at a real date since boot: decide what the stored "today"
    // was. Steps counted since boot always belong to the real today.
    pending_valid_ = false;
    dropHistoryFrom(local_day);
    if (pending_day_ == local_day) {
      reported_step_baseline_ += pending_steps_;
      model_.steps += pending_steps_;
      saved_steps_ = pending_steps_;
      out.print(F("Restored today's steps from flash: "));
      out.println(pending_steps_);
    } else if (pending_day_ < local_day) {
      archive(pending_day_, pending_steps_);
      changed = true;
      out.println(F("Stored step count was for an earlier day: archived."));
    } else {
      changed = true;
      out.println(F("Stored step count is dated in the future: discarded."));
    }
  }
  if (local_day != current_day_) {
    if (current_day_ != 0 && local_day == current_day_ + 1) {
      const uint32_t finished = model_.steps;
      archive(current_day_, finished);
      out.print(F("Local midnight: archived steps "));
      out.println(finished);
      resetSteps(out);
      changed = true;
    } else {
      dropHistoryFrom(local_day);
      changed |= current_day_ != 0;  // clock was set to another day
    }
    current_day_ = local_day;
  }
  if (changed) {
    save(out);
  }
}

void WatchActivity::save(Print& out) {
  last_save_ms_ = millis();
  save_requested_ = false;
  saved_steps_ = model_.steps;
  if (current_day_ == 0 || !store_.available()) {
    return;
  }
  StoredSteps stored{};
  stored.today_day = current_day_;
  stored.today_steps = model_.steps;
  stored.count = history_count_;
  for (uint8_t index = 0; index < history_count_; ++index) {
    stored.history[index] = {history_[index].local_day,
                             history_[index].steps};
  }
  if (!store_.saveSteps(stored)) {
    out.println(F("[WARN] Step history could not be saved to flash."));
  }
}

void WatchActivity::serviceStorage(Print& out) {
  // current_day_ == 0 also covers "stored today still pending": the flash
  // copy must not be overwritten before it has been reconciled.
  if (current_day_ == 0) {
    return;
  }
  const uint32_t elapsed = millis() - last_save_ms_;
  const bool due = save_requested_
                       ? elapsed >= RESET_SAVE_INTERVAL_MS
                       : (model_.steps != saved_steps_ &&
                          elapsed >= SAVE_INTERVAL_MS);
  if (due) {
    save(out);
  }
}

void WatchActivity::saveNow(Print& out) {
  if (current_day_ != 0 &&
      (save_requested_ || model_.steps != saved_steps_)) {
    save(out);
  }
}

uint8_t WatchActivity::historyCount() const {
  return current_day_ == 0 ? history_count_
                           : static_cast<uint8_t>(history_count_ + 1);
}

bool WatchActivity::historyEntry(uint8_t index, uint32_t& yyyymmdd,
                                 uint32_t& steps) const {
  if (index < history_count_) {
    yyyymmdd = yyyymmddFromDay(history_[index].local_day);
    steps = history_[index].steps;
    return true;
  }
  if (index == history_count_ && current_day_ != 0) {
    yyyymmdd = yyyymmddFromDay(current_day_);
    steps = model_.steps;
    return true;
  }
  return false;
}

void WatchActivity::sendSleepMovement(uint32_t max_delta_mg, Print& out) {
  char message[36]{};
  snprintf(message, sizeof(message), "SLEEP_DATA|%lu",
           static_cast<unsigned long>(max_delta_mg));
  if (!ble_.send(message)) {
    out.println(F("Sleep movement sample dropped: phone not connected."));
  }
}

void WatchActivity::resetSteps(Print& out) {
  uint8_t activity_type = 0;
  uint32_t sensor_steps = 0;
  if (hardware_activity_ready_ &&
      accelerometer_.readStepCount(sensor_steps, activity_type)) {
    sensor_step_baseline_ = sensor_steps;
  }
  reported_step_baseline_ = 0;
  model_.steps = 0;
  // Persisted by serviceStorage() (rate limited) or the midnight save.
  save_requested_ = true;
  out.println(F("BMA400 step baseline reset."));
}

void WatchActivity::serviceInterrupt(Print& out) {
  if (!hardware_activity_ready_) {
    return;
  }

  uint16_t status = 0;
  if (!accelerometer_.readInterruptStatus(status)) {
    return;
  }

  if ((status & BMA400_ASSERTED_STEP_INT) != 0) {
    uint32_t sensor_steps = 0;
    uint8_t activity_type = 0;
    if (accelerometer_.readStepCount(sensor_steps, activity_type)) {
      const uint32_t delta =
          (sensor_steps - sensor_step_baseline_) & 0x00FFFFFFUL;
      model_.steps = reported_step_baseline_ + delta;
    }
  }

  if (sleep_tracking_ && (status & BMA400_ASSERTED_DRDY_INT) != 0) {
    processSleepSample(out);
  }
}

void WatchActivity::processSleepSample(Print& out) {
  const uint32_t now = millis();

  AccelReading reading{};
  if (!accelerometer_.read(reading)) {
    return;
  }
  const int32_t x = lroundf(reading.x_g * 1000.0f);
  const int32_t y = lroundf(reading.y_g * 1000.0f);
  const int32_t z = lroundf(reading.z_g * 1000.0f);
  model_.accel_x_mg = static_cast<int16_t>(x);
  model_.accel_y_mg = static_cast<int16_t>(y);
  model_.accel_z_mg = static_cast<int16_t>(z);
  model_.accelerometer_valid = true;

  if (sleep_accel_seeded_) {
    const int32_t dx = x - sleep_last_x_mg_;
    const int32_t dy = y - sleep_last_y_mg_;
    const int32_t dz = z - sleep_last_z_mg_;
    const int64_t delta_sq =
        static_cast<int64_t>(dx) * dx + static_cast<int64_t>(dy) * dy +
        static_cast<int64_t>(dz) * dz;
    const uint32_t delta_mg =
        static_cast<uint32_t>(lroundf(sqrtf(static_cast<float>(delta_sq))));
    sleep_max_delta_mg_ = max(sleep_max_delta_mg_, delta_mg);
  }
  sleep_last_x_mg_ = static_cast<int16_t>(x);
  sleep_last_y_mg_ = static_cast<int16_t>(y);
  sleep_last_z_mg_ = static_cast<int16_t>(z);
  sleep_accel_seeded_ = true;

  if (now - sleep_interval_started_at_ >= SLEEP_REPORT_INTERVAL_MS) {
    const bool paused =
        sleep_pause_until_epoch_ != 0 &&
        static_cast<uint32_t>(rtc_.epoch()) < sleep_pause_until_epoch_;
    sendSleepMovement(paused ? 0 : sleep_max_delta_mg_, out);
    sleep_interval_started_at_ = now;
    sleep_max_delta_mg_ = 0;
  }
}
