#pragma once

#include <Arduino.h>

#include <ET011TJ1.h>

#include "i2c_devices.h"
#include "watch_buttons.h"

class WatchDiagnostics {
 public:
  WatchDiagnostics(I2cBus& i2c, Bma400& accelerometer, Bme280& environment,
                   Max17048& fuel_gauge, ET011TJ1& display, WatchButtons& buttons,
                   bool& display_light_on);

  void initializePeripherals();
  void runAll(Print& out);
  void printSensors(Print& out);
  void printGpio(Print& out);

  void armDisplay(Print& out);
  void runDisplayTest(ET011TJ1Pattern pattern, Print& out);
  void probeDisplay(Print& out);

  bool accelerometerReady() const;
  bool environmentReady() const;
  bool fuelGaugeReady() const;

 private:
  static constexpr uint32_t DISPLAY_ARM_WINDOW_MS = 10000;

  static void printHexByte(Print& out, uint8_t value);
  static void printPass(Print& out, bool passed);
  bool printAccelerometer(Print& out);
  bool printEnvironment(Print& out);
  bool printBattery(Print& out);
  bool consumeDisplayArm(Print& out);

  I2cBus& i2c_;
  Bma400& accelerometer_;
  Bme280& environment_;
  Max17048& fuel_gauge_;
  ET011TJ1& display_;
  WatchButtons& buttons_;
  bool& display_light_on_;
  bool accelerometer_ready_ = false;
  bool environment_ready_ = false;
  bool fuel_gauge_ready_ = false;
  uint32_t display_armed_until_ = 0;
};
