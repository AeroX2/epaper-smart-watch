#pragma once

#include <Arduino.h>
#include <Wire.h>

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
  bool read(uint8_t address, uint8_t reg, uint8_t* data, size_t length);
  bool write(uint8_t address, uint8_t reg, const uint8_t* data, size_t length);
  bool writeByte(uint8_t address, uint8_t reg, uint8_t value);
  bool readByte(uint8_t address, uint8_t reg, uint8_t& value);
  bool readWordBe(uint8_t address, uint8_t reg, uint16_t& value);

 private:
  TwoWire& wire_;
};

class Bma400 {
 public:
  explicit Bma400(I2cBus& bus);

  bool begin();
  bool read(AccelReading& reading);
  uint8_t address() const;
  uint8_t chipId() const;

 private:
  static int16_t decode12(uint8_t lsb, uint8_t msb);

  I2cBus& bus_;
  uint8_t address_ = 0;
  uint8_t chip_id_ = 0;
};

class Bmx280 {
 public:
  explicit Bmx280(I2cBus& bus);

  bool begin();
  bool read(EnvironmentReading& reading);
  uint8_t address() const;
  uint8_t chipId() const;

 private:
  struct Calibration {
    uint16_t t1;
    int16_t t2;
    int16_t t3;
    uint16_t p1;
    int16_t p2;
    int16_t p3;
    int16_t p4;
    int16_t p5;
    int16_t p6;
    int16_t p7;
    int16_t p8;
    int16_t p9;
    uint8_t h1;
    int16_t h2;
    uint8_t h3;
    int16_t h4;
    int16_t h5;
    int8_t h6;
  };

  static uint16_t u16le(const uint8_t* data);
  static int16_t s16le(const uint8_t* data);
  bool readCalibration();
  int32_t compensateTemperature(int32_t raw, int32_t& fine) const;
  uint32_t compensatePressure(int32_t raw, int32_t fine) const;
  uint32_t compensateHumidity(int32_t raw, int32_t fine) const;

  I2cBus& bus_;
  Calibration calibration_{};
  uint8_t address_ = 0;
  uint8_t chip_id_ = 0;
};

class Max17048 {
 public:
  explicit Max17048(I2cBus& bus);

  bool begin();
  bool read(BatteryReading& reading);

 private:
  static constexpr uint8_t ADDRESS = 0x36;
  I2cBus& bus_;
};
