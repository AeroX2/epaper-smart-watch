#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

#include <ET011TJ1.h>

#include "board_pins.h"
#include "i2c_devices.h"
#include "qspi_flash.h"
#include "touch_sense.h"
#include "stm32wbxx_hal.h"

namespace {

TwoWire watch_wire(BoardPins::I2C_SDA, BoardPins::I2C_SCL);
I2cBus i2c(watch_wire);
Bma400 accelerometer(i2c);
Bmx280 environment(i2c);
Max17048 fuel_gauge(i2c);
QspiFlash flash;
TouchSense touch;
SPIClass display_spi(BoardPins::DISPLAY_MOSI, BoardPins::DISPLAY_DUMMY_MISO,
                     BoardPins::DISPLAY_SCK);
ET011TJ1 display(display_spi, BoardPins::DISPLAY_CS, BoardPins::DISPLAY_DC,
                 BoardPins::DISPLAY_RESET, BoardPins::DISPLAY_BUSY);

bool accelerometer_ready = false;
bool environment_ready = false;
bool fuel_gauge_ready = false;
bool flash_ready = false;
bool touch_ready = false;
bool display_light_on = false;
uint32_t display_armed_until = 0;

constexpr uint32_t DISPLAY_ARM_WINDOW_MS = 10000;

constexpr uint32_t BUTTON_PINS[] = {
    BoardPins::BUTTON_1,
    BoardPins::BUTTON_2,
    BoardPins::BUTTON_3,
    BoardPins::BUTTON_4,
};

bool last_button_state[] = {false, false, false, false};

void printHexByte(uint8_t value) {
  if (value < 0x10) {
    Serial.print('0');
  }
  Serial.print(value, HEX);
}

void printPass(bool passed) {
  Serial.print(passed ? F("[PASS] ") : F("[FAIL] "));
}

void configureGpio() {
  // Safe nPM1100 defaults: automatic buck mode and the lower USB current limit.
  pinMode(BoardPins::CHARGER_ISET, OUTPUT);
  digitalWrite(BoardPins::CHARGER_ISET, LOW);
  pinMode(BoardPins::CHARGER_MODE, OUTPUT);
  digitalWrite(BoardPins::CHARGER_MODE, LOW);
  pinMode(BoardPins::SHIP_HOLD, INPUT);

  pinMode(BoardPins::VIBRATION, OUTPUT);
  digitalWrite(BoardPins::VIBRATION, LOW);
  pinMode(BoardPins::BUZZER, OUTPUT);
  digitalWrite(BoardPins::BUZZER, LOW);

  // Recovery state: do not allow either display boost converter to start.
  // Preload each output latch before changing the pin mode to avoid a pulse.
  digitalWrite(BoardPins::DISPLAY_LIGHT, LOW);
  pinMode(BoardPins::DISPLAY_LIGHT, OUTPUT);
  digitalWrite(BoardPins::DISPLAY_RESET, LOW);
  pinMode(BoardPins::DISPLAY_RESET, OUTPUT);
  digitalWrite(BoardPins::DISPLAY_CS, HIGH);
  pinMode(BoardPins::DISPLAY_CS, OUTPUT);
  digitalWrite(BoardPins::DISPLAY_DC, LOW);
  pinMode(BoardPins::DISPLAY_DC, OUTPUT);
  digitalWrite(BoardPins::DISPLAY_SCK, LOW);
  pinMode(BoardPins::DISPLAY_SCK, OUTPUT);
  digitalWrite(BoardPins::DISPLAY_MOSI, LOW);
  pinMode(BoardPins::DISPLAY_MOSI, OUTPUT);
  pinMode(BoardPins::DISPLAY_BUSY, INPUT_PULLDOWN);

  for (const uint32_t pin : BUTTON_PINS) {
    pinMode(pin, INPUT_PULLUP);
  }
  pinMode(BoardPins::ACCEL_INT1, INPUT);
  pinMode(BoardPins::ACCEL_INT2, INPUT);
  pinMode(BoardPins::BATTERY_ALERT, INPUT_PULLUP);

  for (size_t i = 0; i < 4; ++i) {
    last_button_state[i] = digitalRead(BUTTON_PINS[i]) == LOW;
  }
}

void printBanner() {
  Serial.println();
  Serial.println(F("E-paper Smart Watch - guarded display bring-up"));
  Serial.println(F("ET011TJ1 reset and LED boost default to off"));
  Serial.print(F("MCU device/revision: 0x"));
  Serial.print(HAL_GetDEVID(), HEX);
  Serial.print(F(" / 0x"));
  Serial.println(HAL_GetREVID(), HEX);
  Serial.print(F("Core clock: "));
  Serial.print(SystemCoreClock / 1000000);
  Serial.println(F(" MHz"));
}

void printHelp() {
  Serial.println();
  Serial.println(F("Commands:"));
  Serial.println(F("  a  run all non-destructive tests"));
  Serial.println(F("  s  read accelerometer, environment, and battery sensors"));
  Serial.println(F("  i  scan the I2C bus"));
  Serial.println(F("  f  read external flash JEDEC identity"));
  Serial.println(F("  t  read all three capacitive touch channels"));
  Serial.println(F("  g  read buttons and interrupt/control GPIOs"));
  Serial.println(F("  v  run the vibration motor for 300 ms"));
  Serial.println(F("  b  chirp the buzzer"));
  Serial.println(F("  e  reset/probe display logic (booster stays off)"));
  Serial.println(F("  x  arm one display update for 10 seconds"));
  Serial.println(F("  d  show diagnostic pattern (requires x first)"));
  Serial.println(F("  w  refresh display to white (requires x first)"));
  Serial.println(F("  k  refresh display to black (requires x first)"));
  Serial.println(F("  l  toggle the display LED ring"));
  Serial.println(F("  h  show this help"));
  Serial.println();
  Serial.println(F("Actuators only run on explicit v/b commands."));
  Serial.println(F("Flash testing is read-only; no user data is erased."));
  Serial.println(F("Each display update requires x and ends in booster-off standby."));
}

bool printAccelerometer() {
  AccelReading reading{};
  const bool ok = accelerometer_ready && accelerometer.read(reading);
  printPass(ok);
  Serial.print(F("BMA400"));
  if (ok) {
    Serial.print(F(" @0x"));
    printHexByte(accelerometer.address());
    Serial.print(F(" id=0x"));
    printHexByte(accelerometer.chipId());
    Serial.print(F(" xyz[g]="));
    Serial.print(reading.x_g, 3);
    Serial.print(',');
    Serial.print(reading.y_g, 3);
    Serial.print(',');
    Serial.print(reading.z_g, 3);
    Serial.print(F(" temp="));
    Serial.print(reading.temperature_c, 1);
    Serial.print(F(" C"));
  }
  Serial.println();
  return ok;
}

bool printEnvironment() {
  EnvironmentReading reading{};
  const bool ok = environment_ready && environment.read(reading);
  printPass(ok);
  Serial.print(environment.chipId() == 0x60 ? F("BME280") : F("BMP280"));
  if (ok) {
    Serial.print(F(" @0x"));
    printHexByte(environment.address());
    Serial.print(F(" id=0x"));
    printHexByte(environment.chipId());
    Serial.print(F(" temp="));
    Serial.print(reading.temperature_c, 2);
    Serial.print(F(" C pressure="));
    Serial.print(reading.pressure_hpa, 2);
    Serial.print(F(" hPa"));
    if (reading.has_humidity) {
      Serial.print(F(" humidity="));
      Serial.print(reading.humidity_percent, 1);
      Serial.print(F(" %"));
    }
  }
  Serial.println();
  return ok;
}

bool printBattery() {
  BatteryReading reading{};
  const bool ok = fuel_gauge_ready && fuel_gauge.read(reading);
  printPass(ok);
  Serial.print(F("MAX17048"));
  if (ok) {
    Serial.print(F(" voltage="));
    Serial.print(reading.voltage_v, 3);
    Serial.print(F(" V soc="));
    Serial.print(reading.state_of_charge_percent, 1);
    Serial.print(F(" % rate="));
    Serial.print(reading.rate_percent_per_hour, 2);
    Serial.print(F(" %/h version=0x"));
    Serial.print(reading.version, HEX);
    Serial.print(F(" status=0x"));
    Serial.print(reading.status, HEX);
  }
  Serial.println();
  return ok;
}

void printSensors() {
  printAccelerometer();
  printEnvironment();
  printBattery();
}

bool printFlash() {
  FlashIdentity identity{};
  const bool ok = flash_ready && flash.readIdentity(identity);
  printPass(ok);
  Serial.print(F("XT25F32 QSPI JEDEC"));
  if (ok) {
    Serial.print(F(" = "));
    printHexByte(identity.manufacturer);
    Serial.print(' ');
    printHexByte(identity.memory_type);
    Serial.print(' ');
    printHexByte(identity.capacity);
    if (identity.capacity == 0x16) {
      Serial.print(F(" (32 Mbit)"));
    }
  }
  Serial.println();
  return ok;
}

bool printTouch() {
  TouchReadings readings{};
  const bool ok = touch_ready && touch.read(readings);
  printPass(ok);
  Serial.print(F("touch raw counts"));
  if (ok) {
    Serial.print(F(" = "));
    Serial.print(readings.channel_1);
    Serial.print(F(", "));
    Serial.print(readings.channel_2);
    Serial.print(F(", "));
    Serial.print(readings.channel_3);
    Serial.print(F(" (touching a pad should change its count)"));
  }
  Serial.println();
  return ok;
}

void printGpio() {
  Serial.print(F("buttons (1=pressed): "));
  for (size_t i = 0; i < 4; ++i) {
    Serial.print(digitalRead(BUTTON_PINS[i]) == LOW);
    if (i != 3) {
      Serial.print(' ');
    }
  }
  Serial.println();

  Serial.print(F("BMA400 INT1/INT2: "));
  Serial.print(digitalRead(BoardPins::ACCEL_INT1));
  Serial.print(' ');
  Serial.println(digitalRead(BoardPins::ACCEL_INT2));

  Serial.print(F("MAX17048 ALRT (active low): "));
  Serial.println(digitalRead(BoardPins::BATTERY_ALERT));
  Serial.print(F("nPM1100 ISET/MODE/SHPHLD: "));
  Serial.print(digitalRead(BoardPins::CHARGER_ISET));
  Serial.print(' ');
  Serial.print(digitalRead(BoardPins::CHARGER_MODE));
  Serial.print(' ');
  Serial.println(digitalRead(BoardPins::SHIP_HOLD));
}

void initializePeripherals() {
  accelerometer_ready = accelerometer.begin();
  environment_ready = environment.begin();
  fuel_gauge_ready = fuel_gauge.begin();

  if (!flash_ready) {
    flash_ready = flash.begin();
  }
  if (!touch_ready) {
    touch_ready = touch.begin();
  }
}

void runAllTests() {
  Serial.println();
  Serial.println(F("--- non-destructive peripheral test ---"));
  initializePeripherals();

  const uint8_t i2c_count = i2c.scan(Serial);
  printPass(i2c_count >= 3);
  Serial.print(F("I2C device count = "));
  Serial.println(i2c_count);

  uint8_t passed = 0;
  passed += printAccelerometer();
  passed += printEnvironment();
  passed += printBattery();
  passed += printFlash();
  passed += printTouch();
  printGpio();

  Serial.print(F("Summary: "));
  Serial.print(passed);
  Serial.println(F("/5 functional test groups passed"));
  Serial.println(F("---------------------------------------"));
}

void runVibrationTest() {
  Serial.println(F("Vibration motor: 300 ms at 50% drive"));
  analogWrite(BoardPins::VIBRATION, 128);
  delay(300);
  analogWrite(BoardPins::VIBRATION, 0);
}

void runBuzzerTest() {
  Serial.println(F("Buzzer: 2.4 kHz chirp"));
  tone(BoardPins::BUZZER, 2400, 180);
  delay(220);
  noTone(BoardPins::BUZZER);
}

void armDisplay() {
  display_armed_until = millis() + DISPLAY_ARM_WINDOW_MS;
  Serial.println(F(
      "Display high voltage armed for ONE d/w/k command within 10 seconds."));
}

bool consumeDisplayArm() {
  const bool armed =
      display_armed_until != 0 &&
      static_cast<int32_t>(display_armed_until - millis()) > 0;
  display_armed_until = 0;
  if (!armed) {
    Serial.println(F("[BLOCKED] Press x immediately before d, w, or k."));
  }
  return armed;
}

void restoreButtonAfterDisplaySpi() {
  // PB4 is borrowed only to satisfy the STM32 SPI core's MISO requirement.
  pinMode(BoardPins::BUTTON_4, INPUT_PULLUP);
  last_button_state[3] = digitalRead(BoardPins::BUTTON_4) == LOW;
}

void runDisplayTest(ET011TJ1Pattern pattern) {
  digitalWrite(BoardPins::DISPLAY_LIGHT, LOW);
  display_light_on = false;
  if (!consumeDisplayArm()) {
    return;
  }

  if (!display.show(pattern, Serial)) {
    Serial.println(F("Display update stopped safely at the failed stage."));
  }
  restoreButtonAfterDisplaySpi();
}

void toggleDisplayLight() {
  display_light_on = !display_light_on;
  digitalWrite(BoardPins::DISPLAY_LIGHT, display_light_on ? HIGH : LOW);
  Serial.print(F("Display LED ring: "));
  Serial.println(display_light_on ? F("ON") : F("OFF"));
}

void handleCommand(char command) {
  switch (command) {
    case 'a':
    case 'A':
      runAllTests();
      break;
    case 's':
    case 'S':
      printSensors();
      break;
    case 'i':
    case 'I':
      i2c.scan(Serial);
      break;
    case 'f':
    case 'F':
      printFlash();
      break;
    case 't':
    case 'T':
      printTouch();
      break;
    case 'g':
    case 'G':
      printGpio();
      break;
    case 'v':
    case 'V':
      runVibrationTest();
      break;
    case 'b':
    case 'B':
      runBuzzerTest();
      break;
    case 'e':
    case 'E':
      display.logicProbe(Serial);
      restoreButtonAfterDisplaySpi();
      break;
    case 'x':
    case 'X':
      armDisplay();
      break;
    case 'd':
    case 'D':
      runDisplayTest(ET011TJ1Pattern::Diagnostic);
      break;
    case 'w':
    case 'W':
      runDisplayTest(ET011TJ1Pattern::White);
      break;
    case 'k':
    case 'K':
      runDisplayTest(ET011TJ1Pattern::Black);
      break;
    case 'l':
    case 'L':
      toggleDisplayLight();
      break;
    case 'h':
    case 'H':
    case '?':
      printHelp();
      break;
    case '\r':
    case '\n':
    case ' ':
    case '\t':
      break;
    default:
      Serial.print(F("Unknown command '"));
      Serial.print(command);
      Serial.println(F("'. Press h for help."));
      break;
  }
}

void pollButtons() {
  for (size_t i = 0; i < 4; ++i) {
    const bool pressed = digitalRead(BUTTON_PINS[i]) == LOW;
    if (pressed != last_button_state[i]) {
      last_button_state[i] = pressed;
      if (Serial) {
        Serial.print(F("Button "));
        Serial.print(i + 1);
        Serial.println(pressed ? F(" pressed") : F(" released"));
      }
    }
  }
}

}  // namespace

void setup() {
  configureGpio();
  i2c.begin();

  // Let the nPM1100 finish port detection before attaching the USB console.
  delay(750);
  Serial.begin(115200);
  const uint32_t console_started = millis();
  while (!Serial && millis() - console_started < 3000) {
    delay(10);
  }

  printBanner();
  runAllTests();
  printHelp();
}

void loop() {
  while (Serial.available() > 0) {
    handleCommand(static_cast<char>(Serial.read()));
  }
  pollButtons();
  delay(5);
}
