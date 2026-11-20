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
    last_poll_ms_ = millis();
    out.println(F("[PASS] BMA400 step counter running (polled, no step IRQ)."));
  } else {
    hardware_activity_ready_ = false;
    out.println(F("[WARN] BMA400 interrupt configuration failed."));
  }
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
      pollSteps();  // the finished day gets its last steps
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
  if (hardware_activity_ready_ &&
      millis() - last_poll_ms_ >= STEP_POLL_INTERVAL_MS) {
    pollSteps();
  }
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

uint32_t WatchActivity::msUntilNextService(uint32_t now) const {
  uint32_t best = UINT32_MAX;
  if (hardware_activity_ready_) {
    const uint32_t elapsed = now - last_poll_ms_;
    best = elapsed >= STEP_POLL_INTERVAL_MS ? 0
                                            : STEP_POLL_INTERVAL_MS - elapsed;
  }
  if (current_day_ != 0) {
    const uint32_t elapsed = now - last_save_ms_;
    uint32_t interval = UINT32_MAX;
    if (save_requested_) {
      interval = RESET_SAVE_INTERVAL_MS;
    } else if (model_.steps != saved_steps_) {
      interval = SAVE_INTERVAL_MS;
    }
    if (interval != UINT32_MAX) {
      const uint32_t remaining = elapsed >= interval ? 0 : interval - elapsed;
      best = remaining < best ? remaining : best;
    }
  }
  return best;
}

void WatchActivity::saveNow(Print& out) {
  pollSteps();
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

void WatchActivity::pollSteps() {
  last_poll_ms_ = millis();
  if (!hardware_activity_ready_) {
    return;
  }
  uint32_t sensor_steps = 0;
  uint8_t activity_type = 0;
  if (accelerometer_.readStepCount(sensor_steps, activity_type)) {
    const uint32_t delta =
        (sensor_steps - sensor_step_baseline_) & 0x00FFFFFFUL;
    model_.steps = reported_step_baseline_ + delta;
  }
}

