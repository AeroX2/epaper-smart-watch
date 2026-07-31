#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

#include <math.h>
#include <string.h>

#include <ET011TJ1.h>

#include "board_pins.h"
#include "i2c_devices.h"
#include "qspi_flash.h"
#include "touch_sense.h"
#include "watch_ble.h"
#include "watch_rtc.h"
#include "watch_ui.h"
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
WatchUi watch_ui;
WatchUiModel watch_ui_model;
WatchRtc watch_rtc;
WatchBle watch_ble;

bool accelerometer_ready = false;
bool environment_ready = false;
bool fuel_gauge_ready = false;
bool flash_ready = false;
bool touch_ready = false;
bool display_light_on = false;
bool watch_ui_active = false;
uint32_t display_armed_until = 0;
uint8_t last_rendered_minute = 0xFF;
uint32_t timer_last_tick = 0;
uint32_t next_step_sample_at = 0;
uint32_t last_step_at = 0;
uint32_t sleep_interval_started_at = 0;
uint32_t sleep_pause_until_epoch = 0;
uint32_t sleep_alarm_at = 0;
uint32_t sleep_max_delta_mg = 0;
uint32_t alarm_feedback_at = 0;
uint32_t actuator_off_at = 0;
uint32_t last_alarm_minute_key = 0;
uint32_t next_clock_service_at = 0;
time_t snooze_until = 0;
bool step_peak = false;
bool sleep_tracking = false;
bool sleep_alarm_ringing = false;
bool sleep_accel_seeded = false;
int16_t sleep_last_x_mg = 0;
int16_t sleep_last_y_mg = 0;
int16_t sleep_last_z_mg = 0;
bool serial_capture = false;
char serial_line[48]{};
uint8_t serial_line_length = 0;

constexpr uint32_t DISPLAY_ARM_WINDOW_MS = 10000;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 35;
constexpr uint32_t BUTTON_LONG_PRESS_MS = 800;
constexpr uint32_t STEP_SAMPLE_MS = 50;
constexpr uint32_t STEP_REFRACTORY_MS = 280;
constexpr int64_t STEP_HIGH_MAGNITUDE_SQ = 1450000;
constexpr int64_t STEP_LOW_MAGNITUDE_SQ = 1150000;

constexpr uint32_t BUTTON_PINS[] = {
    BoardPins::BUTTON_1,  // bottom-left: Home
    BoardPins::BUTTON_2,  // top-left: Previous
    BoardPins::BUTTON_3,  // top-right: Next
    BoardPins::BUTTON_4,  // bottom-right: Select
};

bool raw_button_state[] = {false, false, false, false};
bool stable_button_state[] = {false, false, false, false};
uint32_t button_changed_at[] = {0, 0, 0, 0};
uint32_t button_pressed_at[] = {0, 0, 0, 0};

void handleCommand(char command);
void dismissAlarm();
void startAlarm(const __FlashStringHelper* reason, WatchScreen screen);

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
    raw_button_state[i] = digitalRead(BUTTON_PINS[i]) == LOW;
    stable_button_state[i] = raw_button_state[i];
    button_changed_at[i] = millis();
    button_pressed_at[i] = millis();
  }
}

void printBanner() {
  Serial.println();
  Serial.println(F("E-paper Smart Watch - interactive firmware prototype"));
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
  Serial.println(F("  u  render the current watch UI screen"));
  Serial.println(F("  [  previous UI screen/item"));
  Serial.println(F("  ]  next UI screen/item"));
  Serial.println(F("  m  UI back; from browse mode return to clock"));
  Serial.println(F("  o  select the current UI action"));
  Serial.println(F("  j  print the STM32 RTC date/time"));
  Serial.println(F("  r  start BLE advertising (may take up to 10 seconds)"));
  Serial.println(F("  @  set RTC: @YYYY-MM-DDTHH:MM:SS then Enter"));
  Serial.println(F("  !  set alarm: !HH:MM then Enter"));
  Serial.println(F("  h  show this help"));
  Serial.println();
  Serial.println(F("Actuators only run on explicit v/b commands."));
  Serial.println(F("Flash testing is read-only; no user data is erased."));
  Serial.println(F("Pattern tests require x and end in booster-off standby."));
  Serial.println(F("UI commands/buttons are explicit updates and do not require x."));
  Serial.println(F("Physical corners: TL=2 previous, TR=3 next, BL=1 home, BR=4 select."));
  Serial.println(F("Browse: 1/2 cards, 4 enter. Inside: 1/2 adjust, 4 act, 3 back."));
  Serial.println(
      F("When ringing: button 1 or long button 4 dismisses; short 4 snoozes."));
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
  raw_button_state[3] = digitalRead(BoardPins::BUTTON_4) == LOW;
  stable_button_state[3] = raw_button_state[3];
  button_changed_at[3] = millis();
  button_pressed_at[3] = millis();
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

void updateWatchUiSensors() {
  watch_rtc.updateModel(watch_ui_model);
  initializePeripherals();

  EnvironmentReading environment_reading{};
  watch_ui_model.environment_valid =
      environment_ready && environment.read(environment_reading);
  if (watch_ui_model.environment_valid) {
    watch_ui_model.temperature_tenths_c =
        static_cast<int16_t>(lroundf(environment_reading.temperature_c * 10.0f));
    watch_ui_model.pressure_hpa =
        static_cast<uint16_t>(lroundf(environment_reading.pressure_hpa));
    watch_ui_model.humidity_percent =
        environment_reading.has_humidity
            ? static_cast<uint8_t>(constrain(
                  lroundf(environment_reading.humidity_percent), 0L, 100L))
            : 0;
  }

  BatteryReading battery_reading{};
  watch_ui_model.battery_valid =
      fuel_gauge_ready && fuel_gauge.read(battery_reading);
  if (watch_ui_model.battery_valid) {
    watch_ui_model.battery_percent = static_cast<uint8_t>(
        constrain(lroundf(battery_reading.state_of_charge_percent), 0L, 100L));
    watch_ui_model.battery_millivolts =
        static_cast<uint16_t>(lroundf(battery_reading.voltage_v * 1000.0f));
  }

  AccelReading accel_reading{};
  watch_ui_model.accelerometer_valid =
      accelerometer_ready && accelerometer.read(accel_reading);
  if (watch_ui_model.accelerometer_valid) {
    watch_ui_model.accel_x_mg =
        static_cast<int16_t>(lroundf(accel_reading.x_g * 1000.0f));
    watch_ui_model.accel_y_mg =
        static_cast<int16_t>(lroundf(accel_reading.y_g * 1000.0f));
    watch_ui_model.accel_z_mg =
        static_cast<int16_t>(lroundf(accel_reading.z_g * 1000.0f));
  }
  watch_ui_model.led_ring_on = display_light_on;
  watch_ui_model.bluetooth_connected = watch_ble.connected();
  if (watch_ui_model.battery_valid) {
    watch_ble.updateBattery(watch_ui_model.battery_percent);
  }
}

void renderWatchUi() {
  updateWatchUiSensors();
  watch_ui.render(watch_ui_model);

  Serial.print(F("Watch UI: "));
  Serial.print(watch_ui.screenName());
  Serial.println(watch_ui.isInside() ? F(" (inside)") : F(" (browse)"));
  if (!display.showFrame(watch_ui.framebuffer(), WatchUi::framebufferStride(),
                         Serial)) {
    Serial.println(F("Watch UI update stopped safely at the failed stage."));
  }
  restoreButtonAfterDisplaySpi();
  watch_ui_active = true;
  last_rendered_minute = watch_ui_model.minute;
}

bool startBluetooth() {
  if (watch_ble.ready()) {
    return true;
  }
  if (!watch_rtc.usingLse()) {
    Serial.println(
        F("[BLOCKED] BLE requires a working 32.768 kHz LSE on STM32WB5MMG."));
    return false;
  }
  const time_t preserved_epoch = watch_rtc.epoch();
  const uint32_t started = millis();
  const bool started_ok = watch_ble.begin(Serial);
  watch_rtc.restoreAfterBackupReset(preserved_epoch, millis() - started,
                                    Serial);
  watch_ui_model.bluetooth_connected = watch_ble.connected();
  return started_ok;
}

void applyUiAction(WatchUiAction action) {
  switch (action) {
    case WatchUiAction::TimerChanged:
      timer_last_tick = millis();
      break;
    case WatchUiAction::NotificationDismissed:
      watch_ble.sendCommand("NOTIFICATION,DISMISSED");
      break;
    case WatchUiAction::MediaPrevious:
      watch_ble.sendCommand("MEDIA,PREVIOUS");
      break;
    case WatchUiAction::MediaPlayPause:
      watch_ble.sendCommand("MEDIA,PLAY_PAUSE");
      break;
    case WatchUiAction::MediaNext:
      watch_ble.sendCommand("MEDIA,NEXT");
      break;
    case WatchUiAction::BluetoothToggle:
      if (watch_ble.ready()) {
        watch_ble.end(Serial);
      } else {
        startBluetooth();
      }
      break;
    case WatchUiAction::SettingsChanged:
      break;
    default:
      break;
  }

  if (watch_ui_model.led_ring_on != display_light_on) {
    display_light_on = watch_ui_model.led_ring_on;
    digitalWrite(BoardPins::DISPLAY_LIGHT, display_light_on ? HIGH : LOW);
  }
}

void applyWatchUiSelection() {
  applyUiAction(watch_ui.select(watch_ui_model));
}

void sendSleepMovement(uint32_t max_delta_mg) {
  char message[36]{};
  snprintf(message, sizeof(message), "SLEEP,DATA,%lu",
           static_cast<unsigned long>(max_delta_mg));
  if (!watch_ble.sendCommand(message)) {
    Serial.println(F("Sleep movement sample dropped: phone not connected."));
  }
}

template <size_t Size>
void copyPhoneText(char (&destination)[Size], const char* source) {
  strncpy(destination, source, Size - 1);
  destination[Size - 1] = '\0';
}

void processPhoneUpdates() {
  PhoneUpdate update{};
  if (!watch_ble.takeUpdate(update)) {
    return;
  }
  bool should_render = false;
  switch (update.type) {
    case PhoneUpdateType::Time:
      watch_rtc.setEpoch(update.epoch);
      Serial.println(F("BLE time synchronization applied."));
      should_render = watch_ui.screen() == WatchScreen::Clock;
      break;
    case PhoneUpdateType::Notification:
      copyPhoneText(watch_ui_model.notification_sender, update.sender);
      copyPhoneText(watch_ui_model.notification_line_1, update.line_1);
      copyPhoneText(watch_ui_model.notification_line_2, update.line_2);
      watch_ui_model.notification_present = true;
      watch_ui.showScreen(WatchScreen::Notifications);
      should_render = true;
      break;
    case PhoneUpdateType::ClearNotification:
      watch_ui_model.notification_present = false;
      should_render = watch_ui.screen() == WatchScreen::Notifications;
      break;
    case PhoneUpdateType::Weather:
      watch_ui_model.phone_weather_valid = true;
      watch_ui_model.forecast_temperature_c = update.temperature_c;
      watch_ui_model.forecast_high_c = update.high_c;
      watch_ui_model.forecast_low_c = update.low_c;
      copyPhoneText(watch_ui_model.weather_condition, update.condition);
      should_render = watch_ui.screen() == WatchScreen::Weather;
      break;
    case PhoneUpdateType::Music:
      watch_ui_model.music_playing = update.playing;
      watch_ui_model.music_position_seconds = update.position_seconds;
      watch_ui_model.music_duration_seconds = update.duration_seconds;
      copyPhoneText(watch_ui_model.music_title, update.title);
      copyPhoneText(watch_ui_model.music_artist, update.artist);
      should_render = watch_ui.screen() == WatchScreen::Music;
      break;
    case PhoneUpdateType::ResetSteps:
      watch_ui_model.steps = 0;
      should_render = watch_ui.screen() == WatchScreen::Activity;
      break;
    case PhoneUpdateType::SleepStart:
      sleep_tracking = true;
      sleep_accel_seeded = false;
      sleep_max_delta_mg = 0;
      sleep_interval_started_at = millis();
      Serial.println(F("Sleep as Android movement tracking started."));
      break;
    case PhoneUpdateType::SleepStop:
      sleep_tracking = false;
      sleep_pause_until_epoch = 0;
      sleep_max_delta_mg = 0;
      Serial.println(F("Sleep as Android movement tracking stopped."));
      break;
    case PhoneUpdateType::SleepPause:
      sleep_pause_until_epoch = update.value;
      Serial.print(F("Sleep tracking pause-until epoch: "));
      Serial.println(sleep_pause_until_epoch);
      break;
    case PhoneUpdateType::SleepAlarmStart:
      if (update.value == 0) {
        sleep_alarm_at = millis();
      } else if (update.value != UINT32_MAX) {
        sleep_alarm_at = millis() + update.value;
      }
      Serial.println(F("Sleep as Android alarm scheduled."));
      break;
    case PhoneUpdateType::SleepAlarmStop:
      sleep_alarm_at = 0;
      sleep_alarm_ringing = false;
      if (watch_ui_model.alarm_ringing) {
        dismissAlarm();
      }
      break;
    case PhoneUpdateType::SleepNotification:
      copyPhoneText(watch_ui_model.notification_sender, "SLEEP AS ANDROID");
      copyPhoneText(watch_ui_model.notification_line_1, update.line_1);
      copyPhoneText(watch_ui_model.notification_line_2, update.line_2);
      watch_ui_model.notification_present = true;
      watch_ui.showScreen(WatchScreen::Notifications);
      should_render = true;
      break;
    case PhoneUpdateType::SleepHint:
      for (uint32_t i = 0; i < min(update.value, 5UL); ++i) {
        analogWrite(BoardPins::VIBRATION, 128);
        delay(80);
        analogWrite(BoardPins::VIBRATION, 0);
        delay(80);
      }
      break;
    default:
      break;
  }
  if (should_render && watch_ui_active) {
    renderWatchUi();
  }
}

void stopAlarmOutput() {
  analogWrite(BoardPins::VIBRATION, 0);
  noTone(BoardPins::BUZZER);
  actuator_off_at = 0;
}

void dismissAlarm() {
  if (sleep_alarm_ringing) {
    watch_ble.sendCommand("SLEEP,DISMISS");
    sleep_alarm_ringing = false;
  }
  watch_ui_model.alarm_ringing = false;
  snooze_until = 0;
  stopAlarmOutput();
  Serial.println(F("Alarm dismissed."));
}

void snoozeAlarm() {
  if (sleep_alarm_ringing) {
    watch_ble.sendCommand("SLEEP,SNOOZE");
    sleep_alarm_ringing = false;
  }
  watch_ui_model.alarm_ringing = false;
  snooze_until = watch_rtc.epoch() + 5 * 60;
  stopAlarmOutput();
  Serial.println(F("Alarm snoozed for 5 minutes."));
}

void startAlarm(const __FlashStringHelper* reason, WatchScreen screen) {
  if (watch_ui_model.alarm_ringing) {
    return;
  }
  watch_ui_model.alarm_ringing = true;
  watch_ui.showScreen(screen);
  alarm_feedback_at = 0;
  Serial.print(F("Alarm started: "));
  Serial.println(reason);
  if (watch_ui_active) {
    renderWatchUi();
  }
}

void serviceAlarmOutput() {
  const uint32_t now = millis();
  if (actuator_off_at != 0 &&
      static_cast<int32_t>(now - actuator_off_at) >= 0) {
    stopAlarmOutput();
  }
  if (!watch_ui_model.alarm_ringing ||
      now - alarm_feedback_at < 1400) {
    return;
  }
  alarm_feedback_at = now;
  if (watch_ui_model.vibration_on) {
    analogWrite(BoardPins::VIBRATION, 160);
  }
  if (!watch_ui_model.quiet_mode) {
    tone(BoardPins::BUZZER, 2200, 180);
  }
  if (watch_ui_model.vibration_on || !watch_ui_model.quiet_mode) {
    actuator_off_at = now + 220;
  }
}

void serviceClockFeatures() {
  const uint32_t now = millis();
  if (static_cast<int32_t>(now - next_clock_service_at) < 0) {
    serviceAlarmOutput();
    return;
  }
  next_clock_service_at = now + 100;
  watch_rtc.updateModel(watch_ui_model);
  if (watch_ui_model.timer_running) {
    const uint32_t elapsed_seconds = (now - timer_last_tick) / 1000;
    if (elapsed_seconds > 0) {
      timer_last_tick += elapsed_seconds * 1000;
      if (elapsed_seconds >= watch_ui_model.timer_remaining_seconds) {
        watch_ui_model.timer_remaining_seconds = 0;
        watch_ui_model.timer_running = false;
        startAlarm(F("timer complete"), WatchScreen::Timer);
      } else {
        watch_ui_model.timer_remaining_seconds -= elapsed_seconds;
      }
    }
  }

  const time_t current_epoch = watch_rtc.epoch();
  if (sleep_alarm_at != 0 &&
      static_cast<int32_t>(now - sleep_alarm_at) >= 0) {
    sleep_alarm_at = 0;
    sleep_alarm_ringing = true;
    startAlarm(F("Sleep as Android"), WatchScreen::Alarms);
  }
  if (snooze_until != 0 && current_epoch >= snooze_until) {
    snooze_until = 0;
    startAlarm(F("snooze complete"), WatchScreen::Alarms);
  }

  const uint32_t minute_key = static_cast<uint32_t>(current_epoch / 60);
  const bool alarm_day =
      (watch_ui_model.weekday >= 1 && watch_ui_model.weekday <= 5)
          ? watch_ui_model.weekday_alarm_on
          : watch_ui_model.weekend_alarm_on;
  if (alarm_day && watch_ui_model.hour == watch_ui_model.alarm_hour &&
      watch_ui_model.minute == watch_ui_model.alarm_minute &&
      minute_key != last_alarm_minute_key) {
    last_alarm_minute_key = minute_key;
    startAlarm(F("scheduled alarm"), WatchScreen::Alarms);
  }
  serviceAlarmOutput();
}

void sampleStepCounter() {
  const uint32_t now = millis();
  if (!accelerometer_ready ||
      static_cast<int32_t>(now - next_step_sample_at) < 0) {
    return;
  }
  next_step_sample_at = now + STEP_SAMPLE_MS;
  AccelReading reading{};
  if (!accelerometer.read(reading)) {
    return;
  }
  const int32_t x = lroundf(reading.x_g * 1000.0f);
  const int32_t y = lroundf(reading.y_g * 1000.0f);
  const int32_t z = lroundf(reading.z_g * 1000.0f);
  watch_ui_model.accel_x_mg = static_cast<int16_t>(x);
  watch_ui_model.accel_y_mg = static_cast<int16_t>(y);
  watch_ui_model.accel_z_mg = static_cast<int16_t>(z);
  watch_ui_model.accelerometer_valid = true;
  if (sleep_tracking) {
    if (sleep_accel_seeded) {
      const int32_t dx = x - sleep_last_x_mg;
      const int32_t dy = y - sleep_last_y_mg;
      const int32_t dz = z - sleep_last_z_mg;
      const int64_t delta_sq =
          static_cast<int64_t>(dx) * dx + static_cast<int64_t>(dy) * dy +
          static_cast<int64_t>(dz) * dz;
      const uint32_t delta_mg =
          static_cast<uint32_t>(lroundf(sqrtf(static_cast<float>(delta_sq))));
      sleep_max_delta_mg = max(sleep_max_delta_mg, delta_mg);
    }
    sleep_last_x_mg = static_cast<int16_t>(x);
    sleep_last_y_mg = static_cast<int16_t>(y);
    sleep_last_z_mg = static_cast<int16_t>(z);
    sleep_accel_seeded = true;

    if (now - sleep_interval_started_at >= 10000) {
      const bool paused =
          sleep_pause_until_epoch != 0 &&
          static_cast<uint32_t>(watch_rtc.epoch()) < sleep_pause_until_epoch;
      sendSleepMovement(paused ? 0 : sleep_max_delta_mg);
      sleep_interval_started_at = now;
      sleep_max_delta_mg = 0;
    }
  }
  const int64_t magnitude_sq =
      static_cast<int64_t>(x) * x + static_cast<int64_t>(y) * y +
      static_cast<int64_t>(z) * z;
  if (!step_peak && magnitude_sq > STEP_HIGH_MAGNITUDE_SQ &&
      now - last_step_at >= STEP_REFRACTORY_MS) {
    ++watch_ui_model.steps;
    last_step_at = now;
    step_peak = true;
  } else if (step_peak && magnitude_sq < STEP_LOW_MAGNITUDE_SQ) {
    step_peak = false;
  }
}

void parseCapturedCommand() {
  serial_line[serial_line_length] = '\0';
  if (serial_line[0] == '@') {
    unsigned int year = 0;
    unsigned int month = 0;
    unsigned int day = 0;
    unsigned int hour = 0;
    unsigned int minute = 0;
    unsigned int second = 0;
    const bool parsed =
        sscanf(serial_line + 1, "%u-%u-%uT%u:%u:%u", &year, &month, &day,
               &hour, &minute, &second) == 6;
    const bool set = parsed &&
                     watch_rtc.setDateTime(
                         static_cast<uint16_t>(year),
                         static_cast<uint8_t>(month), static_cast<uint8_t>(day),
                         static_cast<uint8_t>(hour),
                         static_cast<uint8_t>(minute),
                         static_cast<uint8_t>(second));
    Serial.println(set ? F("[PASS] RTC updated")
                       : F("[FAIL] Use @YYYY-MM-DDTHH:MM:SS"));
    watch_rtc.print(Serial);
  } else if (serial_line[0] == '!') {
    unsigned int hour = 0;
    unsigned int minute = 0;
    if (sscanf(serial_line + 1, "%u:%u", &hour, &minute) == 2 && hour < 24 &&
        minute < 60) {
      watch_ui_model.alarm_hour = static_cast<uint8_t>(hour);
      watch_ui_model.alarm_minute = static_cast<uint8_t>(minute);
      watch_ui_model.weekday_alarm_on = true;
      Serial.println(F("[PASS] Weekday alarm updated"));
    } else {
      Serial.println(F("[FAIL] Use !HH:MM"));
    }
  }
}

void handleSerialByte(char value) {
  if (!serial_capture && (value == '@' || value == '!')) {
    serial_capture = true;
    serial_line_length = 0;
    serial_line[serial_line_length++] = value;
    return;
  }
  if (!serial_capture) {
    handleCommand(value);
    return;
  }
  if (value == '\r' || value == '\n') {
    parseCapturedCommand();
    serial_capture = false;
    serial_line_length = 0;
    return;
  }
  if (serial_line_length < sizeof(serial_line) - 1) {
    serial_line[serial_line_length++] = value;
  }
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
    case 'u':
    case 'U':
      renderWatchUi();
      break;
    case '[':
      applyUiAction(watch_ui.previous(watch_ui_model));
      renderWatchUi();
      break;
    case ']':
      applyUiAction(watch_ui.next(watch_ui_model));
      renderWatchUi();
      break;
    case 'm':
    case 'M':
      watch_ui.home();
      renderWatchUi();
      break;
    case 'o':
    case 'O':
      applyWatchUiSelection();
      renderWatchUi();
      break;
    case 'j':
    case 'J':
      watch_rtc.print(Serial);
      break;
    case 'r':
    case 'R':
      startBluetooth();
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

void handleWatchButton(size_t index, bool long_press) {
  Serial.print(F("Button "));
  Serial.print(index + 1);
  Serial.println(long_press ? F(" long press") : F(" short press"));
  if (watch_ui_model.alarm_ringing) {
    if (index == 0 || (index == 3 && long_press)) {
      dismissAlarm();
    } else if (index == 3) {
      snoozeAlarm();
    }
    renderWatchUi();
    return;
  }
  if (long_press && index == 0) {
    watch_ui.home();
    renderWatchUi();
    return;
  }
  switch (index) {
    case 0:
      watch_ui.home();
      break;
    case 1:
      applyUiAction(watch_ui.previous(watch_ui_model));
      break;
    case 2:
      applyUiAction(watch_ui.next(watch_ui_model));
      break;
    case 3:
      applyWatchUiSelection();
      break;
    default:
      return;
  }
  renderWatchUi();
}

void pollButtons() {
  const uint32_t now = millis();
  for (size_t i = 0; i < 4; ++i) {
    const bool pressed = digitalRead(BUTTON_PINS[i]) == LOW;
    if (pressed != raw_button_state[i]) {
      raw_button_state[i] = pressed;
      button_changed_at[i] = now;
    }
    if (pressed != stable_button_state[i] &&
        now - button_changed_at[i] >= BUTTON_DEBOUNCE_MS) {
      stable_button_state[i] = pressed;
      if (pressed) {
        button_pressed_at[i] = now;
      } else {
        handleWatchButton(
            i, now - button_pressed_at[i] >= BUTTON_LONG_PRESS_MS);
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
  watch_rtc.begin(Serial);
  runAllTests();
  timer_last_tick = millis();
  next_step_sample_at = millis() + STEP_SAMPLE_MS;
  printHelp();
}

void loop() {
  while (Serial.available() > 0) {
    handleSerialByte(static_cast<char>(Serial.read()));
  }
  watch_ble.poll();
  watch_ui_model.bluetooth_connected = watch_ble.connected();
  processPhoneUpdates();
  serviceClockFeatures();
  sampleStepCounter();
  pollButtons();
  if (watch_ui_active && watch_ui.screen() == WatchScreen::Clock &&
      watch_ui_model.minute != last_rendered_minute) {
    renderWatchUi();
  }
  delay(5);
}
