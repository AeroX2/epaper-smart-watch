#include "i2c_devices.h"

#include "board_pins.h"

namespace {

constexpr uint8_t BMA400_ID = 0x90;
constexpr uint8_t BMA400_REG_CHIP_ID = 0x00;
constexpr uint8_t BMA400_REG_ACCEL_DATA = 0x04;
constexpr uint8_t BMA400_REG_TEMP_DATA = 0x11;
constexpr uint8_t BMA400_REG_ACCEL_CONFIG_0 = 0x19;
constexpr uint8_t BMA400_REG_ACCEL_CONFIG_1 = 0x1A;

constexpr uint8_t BMX280_REG_CALIB_00 = 0x88;
constexpr uint8_t BMX280_REG_CALIB_H1 = 0xA1;
constexpr uint8_t BMX280_REG_CALIB_26 = 0xE1;
constexpr uint8_t BMX280_REG_CHIP_ID = 0xD0;
constexpr uint8_t BMX280_REG_CTRL_HUM = 0xF2;
constexpr uint8_t BMX280_REG_CTRL_MEAS = 0xF4;
constexpr uint8_t BMX280_REG_DATA = 0xF7;

constexpr uint8_t BME280_ID = 0x60;
constexpr uint8_t BMP280_ID = 0x58;
constexpr uint8_t BMP280_SAMPLE_ID_1 = 0x56;
constexpr uint8_t BMP280_SAMPLE_ID_2 = 0x57;

}  // namespace

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

bool I2cBus::read(uint8_t address, uint8_t reg, uint8_t* data, size_t length) {
  wire_.beginTransmission(address);
  wire_.write(reg);
  if (wire_.endTransmission(false) != 0) {
    return false;
  }

  const size_t received = wire_.requestFrom(address, length, true);
  if (received != length) {
    while (wire_.available()) {
      wire_.read();
    }
    return false;
  }

  for (size_t i = 0; i < length; ++i) {
    data[i] = static_cast<uint8_t>(wire_.read());
  }
  return true;
}

bool I2cBus::write(uint8_t address, uint8_t reg, const uint8_t* data, size_t length) {
  wire_.beginTransmission(address);
  wire_.write(reg);
  wire_.write(data, length);
  return wire_.endTransmission() == 0;
}

bool I2cBus::writeByte(uint8_t address, uint8_t reg, uint8_t value) {
  return write(address, reg, &value, 1);
}

bool I2cBus::readByte(uint8_t address, uint8_t reg, uint8_t& value) {
  return read(address, reg, &value, 1);
}

bool I2cBus::readWordBe(uint8_t address, uint8_t reg, uint16_t& value) {
  uint8_t bytes[2];
  if (!read(address, reg, bytes, sizeof(bytes))) {
    return false;
  }
  value = static_cast<uint16_t>((static_cast<uint16_t>(bytes[0]) << 8) | bytes[1]);
  return true;
}

Bma400::Bma400(I2cBus& bus) : bus_(bus) {}

bool Bma400::begin() {
  for (const uint8_t candidate : {uint8_t{0x14}, uint8_t{0x15}}) {
    uint8_t id = 0;
    if (bus_.readByte(candidate, BMA400_REG_CHIP_ID, id) && id == BMA400_ID) {
      address_ = candidate;
      chip_id_ = id;
      break;
    }
  }
  if (address_ == 0) {
    return false;
  }

  // 100 Hz, +/-2 g, OSR0, then normal power mode.
  if (!bus_.writeByte(address_, BMA400_REG_ACCEL_CONFIG_1, 0x08) ||
      !bus_.writeByte(address_, BMA400_REG_ACCEL_CONFIG_0, 0x02)) {
    return false;
  }
  delay(10);
  return true;
}

int16_t Bma400::decode12(uint8_t lsb, uint8_t msb) {
  uint16_t value = (static_cast<uint16_t>(msb) << 8) | lsb;
  value &= 0x0FFF;
  if ((value & 0x0800) != 0) {
    value |= 0xF000;
  }
  return static_cast<int16_t>(value);
}

bool Bma400::read(AccelReading& reading) {
  if (address_ == 0) {
    return false;
  }

  uint8_t accel[6];
  uint8_t temperature = 0;
  if (!bus_.read(address_, BMA400_REG_ACCEL_DATA, accel, sizeof(accel)) ||
      !bus_.readByte(address_, BMA400_REG_TEMP_DATA, temperature)) {
    return false;
  }

  // In +/-2 g mode, the 12-bit output scale is 1024 LSB/g.
  reading.x_g = decode12(accel[0], accel[1]) / 1024.0F;
  reading.y_g = decode12(accel[2], accel[3]) / 1024.0F;
  reading.z_g = decode12(accel[4], accel[5]) / 1024.0F;
  reading.temperature_c = static_cast<int8_t>(temperature) * 0.5F + 23.0F;
  return true;
}

uint8_t Bma400::address() const {
  return address_;
}

uint8_t Bma400::chipId() const {
  return chip_id_;
}

Bmx280::Bmx280(I2cBus& bus) : bus_(bus) {}

uint16_t Bmx280::u16le(const uint8_t* data) {
  return static_cast<uint16_t>(data[0] | (static_cast<uint16_t>(data[1]) << 8));
}

int16_t Bmx280::s16le(const uint8_t* data) {
  return static_cast<int16_t>(u16le(data));
}

bool Bmx280::begin() {
  for (const uint8_t candidate : {uint8_t{0x76}, uint8_t{0x77}}) {
    uint8_t id = 0;
    if (bus_.readByte(candidate, BMX280_REG_CHIP_ID, id) &&
        (id == BME280_ID || id == BMP280_ID || id == BMP280_SAMPLE_ID_1 ||
         id == BMP280_SAMPLE_ID_2)) {
      address_ = candidate;
      chip_id_ = id;
      break;
    }
  }
  return address_ != 0 && readCalibration();
}

bool Bmx280::readCalibration() {
  uint8_t primary[24];
  if (!bus_.read(address_, BMX280_REG_CALIB_00, primary, sizeof(primary))) {
    return false;
  }

  calibration_.t1 = u16le(&primary[0]);
  calibration_.t2 = s16le(&primary[2]);
  calibration_.t3 = s16le(&primary[4]);
  calibration_.p1 = u16le(&primary[6]);
  calibration_.p2 = s16le(&primary[8]);
  calibration_.p3 = s16le(&primary[10]);
  calibration_.p4 = s16le(&primary[12]);
  calibration_.p5 = s16le(&primary[14]);
  calibration_.p6 = s16le(&primary[16]);
  calibration_.p7 = s16le(&primary[18]);
  calibration_.p8 = s16le(&primary[20]);
  calibration_.p9 = s16le(&primary[22]);

  if (chip_id_ == BME280_ID) {
    uint8_t humidity_1 = 0;
    uint8_t humidity[7];
    if (!bus_.readByte(address_, BMX280_REG_CALIB_H1, humidity_1) ||
        !bus_.read(address_, BMX280_REG_CALIB_26, humidity, sizeof(humidity))) {
      return false;
    }
    calibration_.h1 = humidity_1;
    calibration_.h2 = s16le(&humidity[0]);
    calibration_.h3 = humidity[2];
    calibration_.h4 =
        static_cast<int16_t>((static_cast<int16_t>(humidity[3]) << 4) | (humidity[4] & 0x0F));
    calibration_.h5 =
        static_cast<int16_t>((static_cast<int16_t>(humidity[5]) << 4) | (humidity[4] >> 4));
    calibration_.h6 = static_cast<int8_t>(humidity[6]);
  }
  return calibration_.t1 != 0 && calibration_.p1 != 0;
}

int32_t Bmx280::compensateTemperature(int32_t raw, int32_t& fine) const {
  const int32_t var1 =
      (((raw >> 3) - (static_cast<int32_t>(calibration_.t1) << 1)) * calibration_.t2) >> 11;
  const int32_t delta = (raw >> 4) - static_cast<int32_t>(calibration_.t1);
  const int32_t var2 = (((delta * delta) >> 12) * calibration_.t3) >> 14;
  fine = var1 + var2;
  return (fine * 5 + 128) >> 8;
}

uint32_t Bmx280::compensatePressure(int32_t raw, int32_t fine) const {
  int64_t var1 = static_cast<int64_t>(fine) - 128000;
  int64_t var2 = var1 * var1 * calibration_.p6;
  var2 += (var1 * calibration_.p5) << 17;
  var2 += static_cast<int64_t>(calibration_.p4) << 35;
  var1 = ((var1 * var1 * calibration_.p3) >> 8) + ((var1 * calibration_.p2) << 12);
  var1 = ((((int64_t{1} << 47) + var1) * calibration_.p1) >> 33);
  if (var1 == 0) {
    return 0;
  }

  int64_t pressure = 1048576 - raw;
  pressure = (((pressure << 31) - var2) * 3125) / var1;
  var1 = (static_cast<int64_t>(calibration_.p9) * (pressure >> 13) * (pressure >> 13)) >> 25;
  var2 = (static_cast<int64_t>(calibration_.p8) * pressure) >> 19;
  pressure = ((pressure + var1 + var2) >> 8) + (static_cast<int64_t>(calibration_.p7) << 4);
  return static_cast<uint32_t>(pressure);
}

uint32_t Bmx280::compensateHumidity(int32_t raw, int32_t fine) const {
  int32_t value = fine - 76800;
  value = (((((raw << 14) - (static_cast<int32_t>(calibration_.h4) << 20) -
              (calibration_.h5 * value)) +
             16384) >>
            15) *
           (((((((value * calibration_.h6) >> 10) *
                  (((value * calibration_.h3) >> 11) + 32768)) >>
                 10) +
                2097152) *
                   calibration_.h2 +
               8192) >>
              14));
  value -= (((((value >> 15) * (value >> 15)) >> 7) * calibration_.h1) >> 4);
  value = max(value, 0L);
  value = min(value, 419430400L);
  return static_cast<uint32_t>(value >> 12);
}

bool Bmx280::read(EnvironmentReading& reading) {
  if (address_ == 0) {
    return false;
  }

  const bool has_humidity = chip_id_ == BME280_ID;
  if (has_humidity && !bus_.writeByte(address_, BMX280_REG_CTRL_HUM, 0x01)) {
    return false;
  }

  // One forced measurement with x1 temperature, pressure, and humidity oversampling.
  if (!bus_.writeByte(address_, BMX280_REG_CTRL_MEAS, 0x25)) {
    return false;
  }
  delay(12);

  uint8_t data[8] = {};
  const size_t length = has_humidity ? sizeof(data) : 6;
  if (!bus_.read(address_, BMX280_REG_DATA, data, length)) {
    return false;
  }

  const int32_t raw_pressure =
      (static_cast<int32_t>(data[0]) << 12) | (static_cast<int32_t>(data[1]) << 4) | (data[2] >> 4);
  const int32_t raw_temperature =
      (static_cast<int32_t>(data[3]) << 12) | (static_cast<int32_t>(data[4]) << 4) | (data[5] >> 4);
  const int32_t raw_humidity = (static_cast<int32_t>(data[6]) << 8) | data[7];

  int32_t fine = 0;
  const int32_t temperature_centi_c = compensateTemperature(raw_temperature, fine);
  const uint32_t pressure_q24_8 = compensatePressure(raw_pressure, fine);

  reading.temperature_c = temperature_centi_c / 100.0F;
  reading.pressure_hpa = pressure_q24_8 / 25600.0F;
  reading.has_humidity = has_humidity;
  reading.humidity_percent =
      has_humidity ? compensateHumidity(raw_humidity, fine) / 1024.0F : NAN;
  return true;
}

uint8_t Bmx280::address() const {
  return address_;
}

uint8_t Bmx280::chipId() const {
  return chip_id_;
}

Max17048::Max17048(I2cBus& bus) : bus_(bus) {}

bool Max17048::begin() {
  uint16_t version = 0;
  return bus_.readWordBe(ADDRESS, 0x08, version) && version != 0x0000 && version != 0xFFFF;
}

bool Max17048::read(BatteryReading& reading) {
  uint16_t vcell = 0;
  uint16_t soc = 0;
  uint16_t version = 0;
  uint16_t rate = 0;
  uint16_t status = 0;
  if (!bus_.readWordBe(ADDRESS, 0x02, vcell) || !bus_.readWordBe(ADDRESS, 0x04, soc) ||
      !bus_.readWordBe(ADDRESS, 0x08, version) || !bus_.readWordBe(ADDRESS, 0x16, rate) ||
      !bus_.readWordBe(ADDRESS, 0x1A, status)) {
    return false;
  }

  reading.voltage_v = vcell * 78.125e-6F;
  reading.state_of_charge_percent = soc / 256.0F;
  reading.rate_percent_per_hour = static_cast<int16_t>(rate) * 0.208F;
  reading.version = version;
  reading.status = status;
  return true;
}
