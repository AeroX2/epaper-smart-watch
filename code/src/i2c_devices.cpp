#include "i2c_devices.h"

#include "watch_watchdog.h"

I2cBus::I2cBus(TwoWire& wire) : wire_(wire) {}

void I2cBus::begin() {
  wire_.begin();
  wire_.setClock(400000);
}

bool I2cBus::recoverIfStormed() {
  const uint32_t storms = watchI2cStormCount();
  if (storms == storms_seen_) {
    return false;
  }
  storms_seen_ = storms;
  // end() resets the HAL handle left busy by the broken transfer; begin()
  // clocks out a slave that is still holding SDA and sets the bus up again.
  wire_.end();
  begin();
  return true;
}

bool I2cBus::probe(uint8_t address) {
  recoverIfStormed();
  wire_.beginTransmission(address);
  return wire_.endTransmission() == 0;
}

uint8_t I2cBus::scan(Print& out) {
  uint8_t count = 0;
  out.print(F("I2C:"));
  for (uint8_t address = 0x08; address <= 0x77; ++address) {
    if (probe(address)) {
      out.print(F(" 0x"));
      if (address < 0x10) {
        out.print('0');
      }
      out.print(address, HEX);
      ++count;
    }
  }
  if (count == 0) {
    out.print(F(" no devices"));
  }
  out.println();
  return count;
}

TwoWire& I2cBus::wire() {
  return wire_;
}

Bma400::Bma400(I2cBus& bus) : bus_(bus) {}

bool Bma400::begin() {
  for (const uint8_t candidate : {uint8_t{BMA400_I2C_ADDRESS_DEFAULT},
                                  uint8_t{BMA400_I2C_ADDRESS_SECONDARY}}) {
    if (device_.beginI2C(candidate, bus_.wire()) == BMA400_OK) {
      address_ = candidate;
      chip_id_ = BMA400_CHIP_ID;
      break;
    }
  }
  if (address_ == 0) {
    return false;
  }

  return device_.setRange(BMA400_RANGE_2G) == BMA400_OK &&
         device_.setODR(BMA400_ODR_100HZ) == BMA400_OK &&
         device_.setOSR(BMA400_ACCEL_OSR_SETTING_0) == BMA400_OK;
}

bool Bma400::read(AccelReading& reading) {
  if (address_ == 0) {
    return false;
  }

  float temperature = NAN;
  if (device_.getSensorData() != BMA400_OK ||
      device_.getTemperature(&temperature) != BMA400_OK) {
    return false;
  }

  reading.x_g = device_.data.accelX;
  reading.y_g = device_.data.accelY;
  reading.z_g = device_.data.accelZ;
  reading.temperature_c = temperature;
  return true;
}

bool Bma400::configureActivityInterrupts() {
  if (address_ == 0) {
    return false;
  }
  // The step counter runs whenever its enable bit is set; mapping it to a
  // pin would only wake the MCU on every single step.
  bma400_step_int_conf step_config{};
  step_config.int_chan = BMA400_UNMAP_INT_PIN;
  return device_.setStepCounterInterrupt(&step_config) == BMA400_OK &&
         device_.setDRDYInterruptChannel(BMA400_INT_CHANNEL_1) == BMA400_OK &&
         device_.setInterruptPinMode(BMA400_INT_CHANNEL_1,
                                     BMA400_INT_PUSH_PULL_ACTIVE_1) ==
             BMA400_OK &&
         device_.enableInterrupt(BMA400_STEP_COUNTER_INT_EN, true) ==
             BMA400_OK &&
         device_.enableInterrupt(BMA400_DRDY_INT_EN, false) == BMA400_OK;
}

bool Bma400::configureGestures() {
  if (address_ == 0) {
    return false;
  }
  bma400_tap_conf tap{};
  tap.axes_sel = BMA400_TAP_Z_AXIS_EN;           // a tap on the watch face
  tap.sensitivity = BMA400_TAP_SENSITIVITY_3;    // 0 most sensitive, 7 least
  tap.tics_th = BMA400_TICS_TH_12_DATA_SAMPLES;
  tap.quiet = BMA400_QUIET_80_DATA_SAMPLES;      // longest gap between taps
  tap.quiet_dt = BMA400_QUIET_DT_8_DATA_SAMPLES; // shortest gap between taps
  tap.int_chan = BMA400_INT_CHANNEL_1;

  bma400_orient_int_conf orient{};
  orient.axes_sel = BMA400_AXIS_XYZ_EN;
  orient.data_src = BMA400_DATA_SRC_ACC_FILT2;
  orient.ref_update = BMA400_ORIENT_REFU_ACC_FILT_LP;  // follows the wrist
  orient.orient_thres = 50;     // 400 mg change
  orient.stability_thres = 16;  // steady within 128 mg ...
  orient.orient_int_dur = 15;   // ... for 150 ms
  orient.int_chan = BMA400_INT_CHANNEL_1;

  return device_.setTapInterrupt(&tap) == BMA400_OK &&
         device_.setOrientationChangeInterrupt(&orient) == BMA400_OK &&
         device_.enableInterrupt(BMA400_DOUBLE_TAP_INT_EN, true) ==
             BMA400_OK &&
         device_.enableInterrupt(BMA400_ORIENT_CHANGE_INT_EN, true) ==
             BMA400_OK &&
         device_.enableInterrupt(BMA400_LATCH_INT_EN, true) == BMA400_OK;
}

bool Bma400::enableDataReadyInterrupt(bool enable) {
  return address_ != 0 &&
         device_.enableInterrupt(BMA400_DRDY_INT_EN, enable) == BMA400_OK;
}

bool Bma400::setTrackingRate(bool tracking) {
  return address_ != 0 &&
         device_.setODR(tracking ? BMA400_ODR_12_5HZ : BMA400_ODR_100HZ) ==
             BMA400_OK;
}

bool Bma400::readInterruptStatus(uint16_t& status) {
  status = 0;
  return address_ != 0 && device_.getInterruptStatus(&status) == BMA400_OK;
}

bool Bma400::readStepCount(uint32_t& count, uint8_t& activity_type) {
  count = 0;
  activity_type = 0;
  return address_ != 0 &&
         device_.getStepCount(&count, &activity_type) == BMA400_OK;
}

uint8_t Bma400::address() const {
  return address_;
}

uint8_t Bma400::chipId() const {
  return chip_id_;
}

Bme280::Bme280(I2cBus& bus) : bus_(bus) {}

bool Bme280::begin() {
  for (const uint8_t candidate : {uint8_t{0x76}, uint8_t{0x77}}) {
    if (device_.begin(candidate, &bus_.wire())) {
      address_ = candidate;
      chip_id_ = static_cast<uint8_t>(device_.sensorID());
      break;
    }
  }
  if (address_ == 0) {
    return false;
  }
  device_.setSampling(Adafruit_BME280::MODE_FORCED,
                      Adafruit_BME280::SAMPLING_X1,
                      Adafruit_BME280::SAMPLING_X1,
                      Adafruit_BME280::SAMPLING_X1,
                      Adafruit_BME280::FILTER_OFF,
                      Adafruit_BME280::STANDBY_MS_0_5);
  return true;
}

bool Bme280::read(EnvironmentReading& reading) {
  if (address_ == 0) {
    return false;
  }

  if (!device_.takeForcedMeasurement()) {
    return false;
  }

  reading.temperature_c = device_.readTemperature();
  reading.pressure_hpa = device_.readPressure() / 100.0F;
  reading.humidity_percent = device_.readHumidity();
  reading.has_humidity = true;
  return !isnan(reading.temperature_c) && !isnan(reading.pressure_hpa) &&
         !isnan(reading.humidity_percent);
}

uint8_t Bme280::address() const {
  return address_;
}

uint8_t Bme280::chipId() const {
  return chip_id_;
}

Max17048::Max17048(I2cBus& bus) : bus_(bus) {}

bool Max17048::readRegister(uint8_t reg, uint16_t& value) {
  bus_.recoverIfStormed();
  TwoWire& wire = bus_.wire();
  wire.beginTransmission(MAX17048_I2CADDR_DEFAULT);
  wire.write(reg);
  if (wire.endTransmission(false) != 0 ||
      wire.requestFrom(static_cast<uint8_t>(MAX17048_I2CADDR_DEFAULT),
                       static_cast<uint8_t>(2)) != 2) {
    return false;
  }
  const uint8_t high = static_cast<uint8_t>(wire.read());
  const uint8_t low = static_cast<uint8_t>(wire.read());
  value = static_cast<uint16_t>(high << 8 | low);
  return true;
}

// One 16-bit transfer; the gauge ignores single-byte register writes.
bool Max17048::writeRegister(uint8_t reg, uint16_t value) {
  bus_.recoverIfStormed();
  TwoWire& wire = bus_.wire();
  wire.beginTransmission(MAX17048_I2CADDR_DEFAULT);
  wire.write(reg);
  wire.write(static_cast<uint8_t>(value >> 8));
  wire.write(static_cast<uint8_t>(value & 0xFF));
  return wire.endTransmission() == 0;
}

bool Max17048::begin() {
  ready_ = false;
  uint16_t version = 0;
  if (!readRegister(MAX1704X_VERSION_REG, version) || version == 0x0000 ||
      version == 0xFFFF) {
    return false;
  }
  // The gauge is not reset: it keeps tracking the cell across firmware
  // restarts. After a real power-up it sets the reset indicator (status high
  // byte, bit 0), which only needs clearing.
  uint16_t status = 0;
  if (readRegister(MAX1704X_STATUS_REG, status) &&
      (status & 0x0100U) != 0) {
    writeRegister(MAX1704X_STATUS_REG, status & ~0x0100U);
  }
  // HIBRT = 0xFFFF keeps the gauge in hibernate mode permanently: it samples
  // every 45 s instead of every 250 ms, which suits a load of well under
  // C/100 and cuts its supply current from about 23 uA to about 4 uA. With
  // the default thresholds every e-paper refresh (a >60 mV dip) would kick it
  // back to active mode for six minutes.
  hibernate_set_ = writeRegister(MAX1704X_HIBRT_REG, 0xFFFF);
  ready_ = true;
  return true;
}

bool Max17048::read(BatteryReading& reading) {
  uint16_t vcell = 0;
  uint16_t soc = 0;
  uint16_t crate = 0;
  uint16_t status = 0;
  if (!ready_ || !readRegister(MAX1704X_VCELL_REG, vcell) ||
      !readRegister(MAX1704X_SOC_REG, soc) ||
      !readRegister(MAX1704X_CRATE_REG, crate) ||
      !readRegister(MAX1704X_VERSION_REG, reading.version) ||
      !readRegister(MAX1704X_STATUS_REG, status)) {
    return false;
  }
  reading.voltage_v = vcell * 78.125F / 1000000.0F;
  reading.state_of_charge_percent = soc / 256.0F;
  reading.rate_percent_per_hour = static_cast<int16_t>(crate) * 0.208F;
  reading.status = (status >> 8) & 0x7F;
  return true;
}
