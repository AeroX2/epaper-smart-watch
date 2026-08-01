#include "watch_activity.h"

#include <math.h>

#include "watch_ble.h"
#include "watch_diagnostics.h"
#include "watch_rtc.h"
#include "watch_ui.h"

WatchActivity::WatchActivity(Bma400& accelerometer,
                             WatchDiagnostics& diagnostics, WatchRtc& rtc,
                             WatchBle& ble, WatchUiModel& model)
    : accelerometer_(accelerometer),
      diagnostics_(diagnostics),
      rtc_(rtc),
      ble_(ble),
      model_(model) {}

void WatchActivity::begin(Print& out) {
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

void WatchActivity::pauseSleepTrackingUntil(uint32_t epoch, Print& out) {
  sleep_pause_until_epoch_ = epoch;
  out.print(F("Sleep tracking pause-until epoch: "));
  out.println(sleep_pause_until_epoch_);
}

void WatchActivity::sendSleepMovement(uint32_t max_delta_mg, Print& out) {
  char message[36]{};
  snprintf(message, sizeof(message), "SLEEP,DATA,%lu",
           static_cast<unsigned long>(max_delta_mg));
  if (!ble_.sendCommand(message)) {
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
