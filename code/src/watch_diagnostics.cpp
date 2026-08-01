#include "watch_diagnostics.h"

#include "board_pins.h"

WatchDiagnostics::WatchDiagnostics(
    I2cBus& i2c, Bma400& accelerometer, Bme280& environment,
    Max17048& fuel_gauge, ET011TJ1& display, WatchButtons& buttons,
    bool& display_light_on)
    : i2c_(i2c),
      accelerometer_(accelerometer),
      environment_(environment),
      fuel_gauge_(fuel_gauge),
      display_(display),
      buttons_(buttons),
      display_light_on_(display_light_on) {}

void WatchDiagnostics::printHexByte(Print& out, uint8_t value) {
  if (value < 0x10) {
    out.print('0');
  }
  out.print(value, HEX);
}

void WatchDiagnostics::printPass(Print& out, bool passed) {
  out.print(passed ? F("[PASS] ") : F("[FAIL] "));
}

void WatchDiagnostics::initializePeripherals() {
  // These library initializers reset their devices, so only retry sensors that
  // have not initialized successfully. Normal UI refreshes must not restart
  // the accelerometer, environmental sensor, or fuel gauge.
  if (!accelerometer_ready_) {
    accelerometer_ready_ = accelerometer_.begin();
  }
  if (!environment_ready_) {
    environment_ready_ = environment_.begin();
  }
  if (!fuel_gauge_ready_) {
    fuel_gauge_ready_ = fuel_gauge_.begin();
  }
}

bool WatchDiagnostics::printAccelerometer(Print& out) {
  AccelReading reading{};
  const bool ok = accelerometer_ready_ && accelerometer_.read(reading);
  printPass(out, ok);
  out.print(F("BMA400"));
  if (ok) {
    out.print(F(" @0x"));
    printHexByte(out, accelerometer_.address());
    out.print(F(" id=0x"));
    printHexByte(out, accelerometer_.chipId());
    out.print(F(" xyz[g]="));
    out.print(reading.x_g, 3);
    out.print(',');
    out.print(reading.y_g, 3);
    out.print(',');
    out.print(reading.z_g, 3);
    out.print(F(" temp="));
    out.print(reading.temperature_c, 1);
    out.print(F(" C"));
  }
  out.println();
  return ok;
}

bool WatchDiagnostics::printEnvironment(Print& out) {
  EnvironmentReading reading{};
  const bool ok = environment_ready_ && environment_.read(reading);
  printPass(out, ok);
  out.print(F("BME280"));
  if (ok) {
    out.print(F(" @0x"));
    printHexByte(out, environment_.address());
    out.print(F(" id=0x"));
    printHexByte(out, environment_.chipId());
    out.print(F(" temp="));
    out.print(reading.temperature_c, 2);
    out.print(F(" C pressure="));
    out.print(reading.pressure_hpa, 2);
    out.print(F(" hPa"));
    if (reading.has_humidity) {
      out.print(F(" humidity="));
      out.print(reading.humidity_percent, 1);
      out.print(F(" %"));
    }
  }
  out.println();
  return ok;
}

bool WatchDiagnostics::printBattery(Print& out) {
  BatteryReading reading{};
  const bool ok = fuel_gauge_ready_ && fuel_gauge_.read(reading);
  printPass(out, ok);
  out.print(F("MAX17048"));
  if (ok) {
    out.print(F(" voltage="));
    out.print(reading.voltage_v, 3);
    out.print(F(" V soc="));
    out.print(reading.state_of_charge_percent, 1);
    out.print(F(" % rate="));
    out.print(reading.rate_percent_per_hour, 2);
    out.print(F(" %/h version=0x"));
    out.print(reading.version, HEX);
    out.print(F(" status=0x"));
    out.print(reading.status, HEX);
  }
  out.println();
  return ok;
}

void WatchDiagnostics::printSensors(Print& out) {
  printAccelerometer(out);
  printEnvironment(out);
  printBattery(out);
}

void WatchDiagnostics::printGpio(Print& out) {
  constexpr uint32_t button_pins[] = {
      BoardPins::BUTTON_1, BoardPins::BUTTON_2,
      BoardPins::BUTTON_3, BoardPins::BUTTON_4,
  };
  out.print(F("buttons (1=pressed): "));
  for (size_t index = 0; index < 4; ++index) {
    out.print(digitalRead(button_pins[index]) == LOW);
    out.print(index == 3 ? '\n' : ' ');
  }
  out.print(F("BMA400 INT1/INT2: "));
  out.print(digitalRead(BoardPins::ACCEL_INT1));
  out.print(' ');
  out.println(digitalRead(BoardPins::ACCEL_INT2));
  out.print(F("MAX17048 ALRT (active low): "));
  out.println(digitalRead(BoardPins::BATTERY_ALERT));
  out.print(F("nPM1100 ISET/MODE/SHPHLD: "));
  out.print(digitalRead(BoardPins::CHARGER_ISET));
  out.print(' ');
  out.print(digitalRead(BoardPins::CHARGER_MODE));
  out.print(' ');
  out.println(digitalRead(BoardPins::SHIP_HOLD));
}

void WatchDiagnostics::runAll(Print& out) {
  out.println();
  out.println(F("--- non-destructive peripheral test ---"));
  initializePeripherals();
  const uint8_t i2c_count = i2c_.scan(out);
  printPass(out, i2c_count >= 3);
  out.print(F("I2C device count = "));
  out.println(i2c_count);
  uint8_t passed = 0;
  passed += printAccelerometer(out);
  passed += printEnvironment(out);
  passed += printBattery(out);
  printGpio(out);
  out.print(F("Summary: "));
  out.print(passed);
  out.println(F("/3 functional test groups passed"));
  out.println(F("---------------------------------------"));
}

void WatchDiagnostics::armDisplay(Print& out) {
  display_armed_until_ = millis() + DISPLAY_ARM_WINDOW_MS;
  out.println(F(
      "Display high voltage armed for ONE d/w/k command within 10 seconds."));
}

bool WatchDiagnostics::consumeDisplayArm(Print& out) {
  const bool armed =
      display_armed_until_ != 0 &&
      static_cast<int32_t>(display_armed_until_ - millis()) > 0;
  display_armed_until_ = 0;
  if (!armed) {
    out.println(F("[BLOCKED] Press x immediately before d, w, or k."));
  }
  return armed;
}

void WatchDiagnostics::runDisplayTest(ET011TJ1Pattern pattern, Print& out) {
  digitalWrite(BoardPins::DISPLAY_LIGHT, LOW);
  display_light_on_ = false;
  if (!consumeDisplayArm(out)) {
    return;
  }
  const bool passed = display_.show(pattern, out);
  if (!passed) {
    out.println(F("Display update stopped safely at the failed stage."));
  }
  buttons_.restoreSharedSpiButton();
}

void WatchDiagnostics::probeDisplay(Print& out) {
  display_.logicProbe(out);
  buttons_.restoreSharedSpiButton();
}

bool WatchDiagnostics::accelerometerReady() const {
  return accelerometer_ready_;
}

bool WatchDiagnostics::environmentReady() const {
  return environment_ready_;
}

bool WatchDiagnostics::fuelGaugeReady() const {
  return fuel_gauge_ready_;
}
