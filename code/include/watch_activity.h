#pragma once

#include <Arduino.h>

#include "i2c_devices.h"

class WatchBle;
class WatchConfigStore;
class WatchDiagnostics;
class WatchRtc;
struct WatchUiModel;

class WatchActivity {
 public:
  WatchActivity(Bma400& accelerometer, WatchDiagnostics& diagnostics,
                WatchRtc& rtc, WatchBle& ble, WatchUiModel& model,
                WatchConfigStore& store);

  void begin(Print& out);
  void serviceInterrupt(Print& out);
  void resetSteps(Print& out);

  void startSleepTracking(Print& out);
  void stopSleepTracking(Print& out);
  // seconds <= 0 resumes; otherwise movement reports read 0 for that long.
  void pauseSleepTracking(int32_t seconds, Print& out);

  // Daily step history. Call with the current local day number
  // (local epoch / 86400). Advancing by exactly one day archives the count
  // and starts a new day; any other jump (clock set) just rebases.
  // Nothing is dated while time_trusted is false (clock never set since the
  // RTC lost power): the stored "today" stays pending and is reconciled on
  // the first call with a trusted clock, so model.steps is always "steps
  // today" once the date is known.
  void serviceDay(uint32_t local_day, bool time_trusted, Print& out);
  // Persists today's count at most hourly (and soon after a reset request).
  void serviceStorage(Print& out);
  // Persists unsaved steps immediately; call before a deliberate reboot.
  void saveNow(Print& out);
  // Completed days plus today, oldest first; today is the last entry.
  uint8_t historyCount() const;
  bool historyEntry(uint8_t index, uint32_t& yyyymmdd, uint32_t& steps) const;

 private:
  static constexpr uint32_t SLEEP_REPORT_INTERVAL_MS = 10000;
  static constexpr uint8_t HISTORY_DAYS = 6;  // + today = 7 reported

  struct StepDay {
    uint32_t local_day;
    uint32_t steps;
  };

  static constexpr uint32_t SAVE_INTERVAL_MS = 60UL * 60UL * 1000UL;
  static constexpr uint32_t RESET_SAVE_INTERVAL_MS = 60UL * 1000UL;

  void restore(Print& out);
  void archive(uint32_t day, uint32_t steps);
  void dropHistoryFrom(uint32_t day);
  void save(Print& out);
  void sendSleepMovement(uint32_t max_delta_mg, Print& out);
  void processSleepSample(Print& out);

  Bma400& accelerometer_;
  WatchDiagnostics& diagnostics_;
  WatchRtc& rtc_;
  WatchBle& ble_;
  WatchUiModel& model_;
  WatchConfigStore& store_;

  uint32_t sensor_step_baseline_ = 0;
  uint32_t reported_step_baseline_ = 0;
  bool hardware_activity_ready_ = false;

  bool sleep_tracking_ = false;
  bool sleep_accel_seeded_ = false;
  uint32_t sleep_interval_started_at_ = 0;
  uint32_t sleep_pause_until_epoch_ = 0;
  uint32_t sleep_max_delta_mg_ = 0;
  int16_t sleep_last_x_mg_ = 0;
  int16_t sleep_last_y_mg_ = 0;
  int16_t sleep_last_z_mg_ = 0;

  StepDay history_[HISTORY_DAYS]{};
  uint8_t history_count_ = 0;  // oldest at index 0
  uint32_t current_day_ = 0;   // 0 = not yet known

  // "Today" as read from flash at boot, until the clock is trusted.
  bool pending_valid_ = false;
  uint32_t pending_day_ = 0;
  uint32_t pending_steps_ = 0;
  uint32_t saved_steps_ = 0;
  uint32_t last_save_ms_ = 0;
  bool save_requested_ = false;
};
