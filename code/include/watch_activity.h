#pragma once

#include <Arduino.h>

#include "i2c_devices.h"

class WatchBle;
class WatchDiagnostics;
class WatchRtc;
struct WatchUiModel;

class WatchActivity {
 public:
  WatchActivity(Bma400& accelerometer, WatchDiagnostics& diagnostics,
                WatchRtc& rtc, WatchBle& ble, WatchUiModel& model);

  void begin(Print& out);
  void serviceInterrupt(Print& out);
  void resetSteps(Print& out);

  void startSleepTracking(Print& out);
  void stopSleepTracking(Print& out);
  void pauseSleepTrackingUntil(uint32_t epoch, Print& out);

 private:
  static constexpr uint32_t SLEEP_REPORT_INTERVAL_MS = 10000;

  void sendSleepMovement(uint32_t max_delta_mg, Print& out);
  void processSleepSample(Print& out);

  Bma400& accelerometer_;
  WatchDiagnostics& diagnostics_;
  WatchRtc& rtc_;
  WatchBle& ble_;
  WatchUiModel& model_;

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
};
