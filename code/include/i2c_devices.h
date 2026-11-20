#pragma once

#include <Arduino.h>
#include <Wire.h>

#include <Adafruit_BME280.h>
#include <Adafruit_MAX1704X.h>
#include <SparkFun_BMA400_Arduino_Library.h>

struct AccelReading {
  float x_g;
  float y_g;
  float z_g;
  float temperature_c;
};

struct EnvironmentReading {
  float temperature_c;
  float pressure_hpa;
  float humidity_percent;
  bool has_humidity;
};

struct BatteryReading {
  float voltage_v;
  float state_of_charge_percent;
  float rate_percent_per_hour;
  uint16_t version;
  uint16_t status;
};

class I2cBus {
 public:
  explicit I2cBus(TwoWire& wire);

  void begin();
  // Restarts the bus if the hang monitor had to break an I2C interrupt storm
  // since the last call. Returns true when it did.
  bool recoverIfStormed();
  uint32_t stormsRecovered() const { return storms_seen_; }
  bool probe(uint8_t address);
  uint8_t scan(Print& out);
  TwoWire& wire();

 private:
  TwoWire& wire_;
  uint32_t storms_seen_ = 0;
};

class Bma400 {
 public:
  explicit Bma400(I2cBus& bus);

  bool begin();
  bool read(AccelReading& reading);
  // Step counter running with no interrupt pin mapped (it is read on a slow
  // cadence instead); data-ready routed to INT1 but disabled.
  bool configureActivityInterrupts();
  // Double tap (on the face, Z axis) and orientation change on INT1, with
  // the interrupt latched until readInterruptStatus() so the cause can
  // always be read. Tap sensing needs the 200 Hz data rate set in begin();
  // the step counter has its own fixed 100 Hz path and is not affected.
  bool configureGestures();
  bool readInterruptStatus(uint16_t& status);
  bool readStepCount(uint32_t& count, uint8_t& activity_type);
  uint8_t address() const;
  uint8_t chipId() const;

 private:
  I2cBus& bus_;
  BMA400 device_;
  uint8_t address_ = 0;
  uint8_t chip_id_ = 0;
};

class Bme280 {
 public:
  explicit Bme280(I2cBus& bus);

  bool begin();
  bool read(EnvironmentReading& reading);
  uint8_t address() const;
  uint8_t chipId() const;

 private:
  I2cBus& bus_;
  Adafruit_BME280 device_;
  uint8_t address_ = 0;
  uint8_t chip_id_ = 0;
};

// Talks to the gauge directly. The Adafruit library's begin() sends the
// power-on-reset command on every start: the gauge resets instead of
// acknowledging it, and that NACK in mid-write is what set off the STM32 HAL
// I2C interrupt storm in most boots. The reset also threw away the gauge's
// learned state of charge. The library header is kept for register names.
class Max17048 {
 public:
  explicit Max17048(I2cBus& bus);

  bool begin();
  bool read(BatteryReading& reading);
  // HIBRT was written to "always hibernate" (about 4 uA instead of 23 uA).
  bool hibernating() const { return hibernate_set_; }

 private:
  bool readRegister(uint8_t reg, uint16_t& value);
  bool writeRegister(uint8_t reg, uint16_t value);

  I2cBus& bus_;
  bool ready_ = false;
  bool hibernate_set_ = false;
};
