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
  bool probe(uint8_t address);
  uint8_t scan(Print& out);
  TwoWire& wire();

 private:
  TwoWire& wire_;
};

class Bma400 {
 public:
  explicit Bma400(I2cBus& bus);

  bool begin();
  bool read(AccelReading& reading);
  bool configureActivityInterrupts();
  bool enableDataReadyInterrupt(bool enable);
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

class Max17048 {
 public:
  explicit Max17048(I2cBus& bus);

  bool begin();
  bool read(BatteryReading& reading);

 private:
  I2cBus& bus_;
  Adafruit_MAX17048 device_;
};
