#include "i2c_devices.h"

I2cBus::I2cBus(TwoWire& wire) : wire_(wire) {}

void I2cBus::begin() {
  wire_.begin();
  wire_.setClock(400000);
}

bool I2cBus::probe(uint8_t address) {
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
  bma400_step_int_conf step_config{};
  step_config.int_chan = BMA400_INT_CHANNEL_1;
  return device_.setStepCounterInterrupt(&step_config) == BMA400_OK &&
         device_.setDRDYInterruptChannel(BMA400_INT_CHANNEL_1) == BMA400_OK &&
         device_.setInterruptPinMode(BMA400_INT_CHANNEL_1,
                                     BMA400_INT_PUSH_PULL_ACTIVE_1) ==
             BMA400_OK &&
         device_.enableInterrupt(BMA400_STEP_COUNTER_INT_EN, true) ==
             BMA400_OK &&
         device_.enableInterrupt(BMA400_DRDY_INT_EN, false) == BMA400_OK;
}

bool Bma400::enableDataReadyInterrupt(bool enable) {
  return address_ != 0 &&
         device_.enableInterrupt(BMA400_DRDY_INT_EN, enable) == BMA400_OK;
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

bool Max17048::begin() {
  return device_.begin(&bus_.wire());
}

bool Max17048::read(BatteryReading& reading) {
  if (!device_.isDeviceReady()) {
    return false;
  }

  reading.voltage_v = device_.cellVoltage();
  reading.state_of_charge_percent = device_.cellPercent();
  reading.rate_percent_per_hour = device_.chargeRate();
  reading.version = device_.getICversion();
  reading.status = device_.getAlertStatus();
  return !isnan(reading.voltage_v) &&
         !isnan(reading.state_of_charge_percent) &&
         !isnan(reading.rate_percent_per_hour);
}
