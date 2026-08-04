#include "watch_application.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "board_pins.h"
#include "stm32wbxx_hal.h"

namespace {

template <size_t Size>
void copyPhoneText(char (&destination)[Size], const char* source) {
  strncpy(destination, source, Size - 1);
  destination[Size - 1] = '\0';
}

}  // namespace

WatchApplication::WatchApplication()
    : wire_(BoardPins::I2C_SDA, BoardPins::I2C_SCL),
      i2c_(wire_),
      accelerometer_(i2c_),
      environment_(i2c_),
      fuel_gauge_(i2c_),
      display_spi_(BoardPins::DISPLAY_MOSI, BoardPins::DISPLAY_DUMMY_MISO,
                   BoardPins::DISPLAY_SCK),
      display_(display_spi_, BoardPins::DISPLAY_CS, BoardPins::DISPLAY_DC,
               BoardPins::DISPLAY_RESET, BoardPins::DISPLAY_BUSY),
      display_controller_(display_, ui_, model_, buttons_),
      diagnostics_(i2c_, accelerometer_, environment_, fuel_gauge_, display_,
                   buttons_, display_light_on_),
      activity_(accelerometer_, diagnostics_, rtc_, ble_, model_),
      alarm_(rtc_, ble_, ui_, model_) {}

void WatchApplication::configureGpio() {
  // Safe nPM1100 defaults: automatic buck mode and lower USB current limit.
  pinMode(BoardPins::CHARGER_ISET, OUTPUT);
  digitalWrite(BoardPins::CHARGER_ISET, LOW);
  pinMode(BoardPins::CHARGER_MODE, OUTPUT);
  digitalWrite(BoardPins::CHARGER_MODE, LOW);
  pinMode(BoardPins::SHIP_HOLD, INPUT);

  pinMode(BoardPins::VIBRATION, OUTPUT);
  digitalWrite(BoardPins::VIBRATION, LOW);
  pinMode(BoardPins::BUZZER, OUTPUT);
  digitalWrite(BoardPins::BUZZER, LOW);

  // Recovery state: neither display boost converter may start during boot.
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

  buttons_.begin();
  pinMode(BoardPins::ACCEL_INT1, INPUT);
  pinMode(BoardPins::ACCEL_INT2, INPUT);
  pinMode(BoardPins::BATTERY_ALERT, INPUT_PULLUP);
}

void WatchApplication::printBanner() {
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

void WatchApplication::printHelp() {
  Serial.println();
  Serial.println(F("Commands:"));
  Serial.println(F("  a  run all non-destructive tests"));
  Serial.println(F("  s  read accelerometer, environment, and battery sensors"));
  Serial.println(F("  i  scan the I2C bus"));
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
  Serial.println(F("  U  force a full cleaning waveform on the current UI"));
  Serial.println(F("  [/]  previous/next UI screen or item"));
  Serial.println(F("  m/o  UI home/select"));
  Serial.println(F("  j  print the STM32 RTC date/time"));
  Serial.println(F("  r  start BLE advertising (may take up to 10 seconds)"));
  Serial.println(F("  f  reboot into resident STM32 USB DFU"));
  Serial.println(F("  @  set RTC: @YYYY-MM-DDTHH:MM:SS then Enter"));
  Serial.println(F("  !  set alarm: !HH:MM then Enter"));
  Serial.println(F("  h  show this help"));
  Serial.println();
  Serial.println(F("Actuators only run on explicit v/b commands."));
  Serial.println(F("Pattern tests require x and end in booster-off standby."));
  Serial.println(F("Boot clears the panel and renders the clock automatically."));
  Serial.println(F("Physical corners: TL=2 previous, TR=3 next, BL=1 home, BR=4 select."));
  Serial.println(F("Diagonal \\: pinch=previous, hold=select."));
  Serial.println(F("Diagonal /: pinch=next, hold=back/home."));
}

void WatchApplication::runVibrationTest() {
  Serial.println(F("Vibration motor: 300 ms at 50% drive"));
  analogWrite(BoardPins::VIBRATION, 128);
  delay(300);
  analogWrite(BoardPins::VIBRATION, 0);
}

void WatchApplication::runBuzzerTest() {
  Serial.println(F("Buzzer: 2.4 kHz chirp"));
  tone(BoardPins::BUZZER, 2400, 180);
  delay(220);
  noTone(BoardPins::BUZZER);
}

void WatchApplication::toggleDisplayLight() {
  display_light_on_ = !display_light_on_;
  digitalWrite(BoardPins::DISPLAY_LIGHT, display_light_on_ ? HIGH : LOW);
  Serial.print(F("Display LED ring: "));
  Serial.println(display_light_on_ ? F("ON") : F("OFF"));
}

void WatchApplication::enterDfu() {
  if (!dfu_.available()) {
    dfu_.request(Serial);
    return;
  }
  display_.shutdown();
  display_light_on_ = false;
  digitalWrite(BoardPins::DISPLAY_LIGHT, LOW);
  analogWrite(BoardPins::VIBRATION, 0);
  noTone(BoardPins::BUZZER);
  if (ble_.ready()) {
    ble_.end(Serial);
  }
  Serial.println(F("Rebooting into STM32 USB DFU..."));
  Serial.flush();
  delay(25);
  dfu_.request(Serial);
}

void WatchApplication::updateUiSensors() {
  rtc_.updateModel(model_);
  diagnostics_.initializePeripherals();

  EnvironmentReading environment{};
  model_.environment_valid =
      diagnostics_.environmentReady() && environment_.read(environment);
  if (model_.environment_valid) {
    model_.temperature_tenths_c =
        static_cast<int16_t>(lroundf(environment.temperature_c * 10.0f));
    model_.pressure_hpa =
        static_cast<uint16_t>(lroundf(environment.pressure_hpa));
    model_.humidity_percent =
        environment.has_humidity
            ? static_cast<uint8_t>(constrain(
                  lroundf(environment.humidity_percent), 0L, 100L))
            : 0;
  }

  BatteryReading battery{};
  model_.battery_valid =
      diagnostics_.fuelGaugeReady() && fuel_gauge_.read(battery);
  if (model_.battery_valid) {
    model_.battery_percent = static_cast<uint8_t>(
        constrain(lroundf(battery.state_of_charge_percent), 0L, 100L));
    model_.battery_millivolts =
        static_cast<uint16_t>(lroundf(battery.voltage_v * 1000.0f));
  }

  model_.led_ring_on = display_light_on_;
  model_.bluetooth_connected = ble_.connected();
  if (model_.battery_valid) {
    ble_.updateBattery(model_.battery_percent);
  }
}

void WatchApplication::renderUi() {
  updateUiSensors();
  display_controller_.render(Serial);
}

bool WatchApplication::startBluetooth() {
  if (ble_.ready()) {
    return true;
  }
  if (!rtc_.usingLse()) {
    Serial.println(
        F("[BLOCKED] BLE requires a working 32.768 kHz LSE on STM32WB5MMG."));
    return false;
  }
  const time_t preserved_epoch = rtc_.epoch();
  const uint32_t started = millis();
  const bool started_ok = ble_.begin(Serial);
  rtc_.restoreAfterBackupReset(preserved_epoch, millis() - started, Serial);
  model_.bluetooth_connected = ble_.connected();
  return started_ok;
}

void WatchApplication::applyUiAction(WatchUiAction action) {
  switch (action) {
    case WatchUiAction::TimerChanged:
      alarm_.timerChanged();
      break;
    case WatchUiAction::NotificationDismissed:
      ble_.sendCommand("NOTIFICATION,DISMISSED");
      break;
    case WatchUiAction::MediaPrevious:
      ble_.sendCommand("MEDIA,PREVIOUS");
      break;
    case WatchUiAction::MediaPlayPause:
      ble_.sendCommand("MEDIA,PLAY_PAUSE");
      break;
    case WatchUiAction::MediaNext:
      ble_.sendCommand("MEDIA,NEXT");
      break;
    case WatchUiAction::BluetoothToggle:
      if (ble_.ready()) {
        ble_.end(Serial);
      } else {
        startBluetooth();
      }
      break;
    default:
      break;
  }

  if (model_.led_ring_on != display_light_on_) {
    display_light_on_ = model_.led_ring_on;
    digitalWrite(BoardPins::DISPLAY_LIGHT, display_light_on_ ? HIGH : LOW);
  }
}

void WatchApplication::applyUiSelection() {
  applyUiAction(ui_.select(model_));
}

void WatchApplication::processPhoneUpdates() {
  PhoneUpdate update{};
  if (!ble_.takeUpdate(update)) {
    return;
  }
  bool should_render = false;
  switch (update.type) {
    case PhoneUpdateType::Time:
      rtc_.setEpoch(update.epoch);
      Serial.println(F("BLE time synchronization applied."));
      should_render = ui_.screen() == WatchScreen::Clock;
      break;
    case PhoneUpdateType::Notification:
      copyPhoneText(model_.notification_sender, update.sender);
      copyPhoneText(model_.notification_line_1, update.line_1);
      copyPhoneText(model_.notification_line_2, update.line_2);
      model_.notification_present = true;
      // A notification should neither steal the screen nor begin a multi-second
      // e-paper refresh from inside BLE processing. The clock picks up its
      // small pending-message indicator on the next minute/input refresh.
      should_render = false;
      break;
    case PhoneUpdateType::ClearNotification:
      model_.notification_present = false;
      should_render = ui_.screen() == WatchScreen::Notifications;
      break;
    case PhoneUpdateType::Weather:
      model_.phone_weather_valid = true;
      model_.forecast_temperature_c = update.temperature_c;
      model_.forecast_high_c = update.high_c;
      model_.forecast_low_c = update.low_c;
      copyPhoneText(model_.weather_condition, update.condition);
      should_render = ui_.screen() == WatchScreen::Weather;
      break;
    case PhoneUpdateType::Music:
      model_.music_playing = update.playing;
      model_.music_position_seconds = update.position_seconds;
      model_.music_duration_seconds = update.duration_seconds;
      copyPhoneText(model_.music_title, update.title);
      copyPhoneText(model_.music_artist, update.artist);
      should_render = ui_.screen() == WatchScreen::Music;
      break;
    case PhoneUpdateType::ResetSteps:
      activity_.resetSteps(Serial);
      should_render = ui_.screen() == WatchScreen::Activity;
      break;
    case PhoneUpdateType::SleepStart:
      activity_.startSleepTracking(Serial);
      break;
    case PhoneUpdateType::SleepStop:
      activity_.stopSleepTracking(Serial);
      break;
    case PhoneUpdateType::SleepPause:
      activity_.pauseSleepTrackingUntil(update.value, Serial);
      break;
    case PhoneUpdateType::SleepAlarmStart:
      alarm_.scheduleSleepAlarm(update.value, Serial);
      break;
    case PhoneUpdateType::SleepAlarmStop:
      should_render |= alarm_.stopSleepAlarm(Serial);
      break;
    case PhoneUpdateType::SleepNotification:
      copyPhoneText(model_.notification_sender, "SLEEP AS ANDROID");
      copyPhoneText(model_.notification_line_1, update.line_1);
      copyPhoneText(model_.notification_line_2, update.line_2);
      model_.notification_present = true;
      should_render = false;
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
  if (should_render && display_controller_.active()) {
    renderUi();
  }
}

void WatchApplication::parseCapturedCommand() {
  serial_line_[serial_line_length_] = '\0';
  if (serial_line_[0] == '@') {
    unsigned int year = 0;
    unsigned int month = 0;
    unsigned int day = 0;
    unsigned int hour = 0;
    unsigned int minute = 0;
    unsigned int second = 0;
    const bool parsed =
        sscanf(serial_line_ + 1, "%u-%u-%uT%u:%u:%u", &year, &month, &day,
               &hour, &minute, &second) == 6;
    const bool set = parsed &&
                     rtc_.setDateTime(
                         static_cast<uint16_t>(year),
                         static_cast<uint8_t>(month), static_cast<uint8_t>(day),
                         static_cast<uint8_t>(hour),
                         static_cast<uint8_t>(minute),
                         static_cast<uint8_t>(second));
    Serial.println(set ? F("[PASS] RTC updated")
                       : F("[FAIL] Use @YYYY-MM-DDTHH:MM:SS"));
    rtc_.print(Serial);
  } else if (serial_line_[0] == '!') {
    unsigned int hour = 0;
    unsigned int minute = 0;
    if (sscanf(serial_line_ + 1, "%u:%u", &hour, &minute) == 2 && hour < 24 &&
        minute < 60) {
      model_.alarm_hour = static_cast<uint8_t>(hour);
      model_.alarm_minute = static_cast<uint8_t>(minute);
      model_.weekday_alarm_on = true;
      Serial.println(F("[PASS] Weekday alarm updated"));
    } else {
      Serial.println(F("[FAIL] Use !HH:MM"));
    }
  }
}

void WatchApplication::handleSerialByte(char value) {
  if (!serial_capture_ && (value == '@' || value == '!')) {
    serial_capture_ = true;
    serial_line_length_ = 0;
    serial_line_[serial_line_length_++] = value;
    return;
  }
  if (!serial_capture_) {
    handleCommand(value);
    return;
  }
  if (value == '\r' || value == '\n') {
    parseCapturedCommand();
    serial_capture_ = false;
    serial_line_length_ = 0;
    return;
  }
  if (serial_line_length_ < sizeof(serial_line_) - 1) {
    serial_line_[serial_line_length_++] = value;
  }
}

void WatchApplication::handleCommand(char command) {
  switch (command) {
    case 'a': case 'A': diagnostics_.runAll(Serial); break;
    case 's': case 'S': diagnostics_.printSensors(Serial); break;
    case 'i': case 'I': i2c_.scan(Serial); break;
    case 'g': case 'G': diagnostics_.printGpio(Serial); break;
    case 'v': case 'V': runVibrationTest(); break;
    case 'b': case 'B': runBuzzerTest(); break;
    case 'e': case 'E': diagnostics_.probeDisplay(Serial); break;
    case 'x': case 'X': diagnostics_.armDisplay(Serial); break;
    case 'd': case 'D':
      diagnostics_.runDisplayTest(ET011TJ1Pattern::Diagnostic, Serial);
      break;
    case 'w': case 'W':
      diagnostics_.runDisplayTest(ET011TJ1Pattern::White, Serial);
      break;
    case 'k': case 'K':
      diagnostics_.runDisplayTest(ET011TJ1Pattern::Black, Serial);
      break;
    case 'l': case 'L': toggleDisplayLight(); break;
    case 'u': renderUi(); break;
    case 'U':
      updateUiSensors();
      display_controller_.renderFull(Serial);
      break;
    case '[':
      applyUiAction(ui_.previous(model_));
      renderUi();
      break;
    case ']':
      applyUiAction(ui_.next(model_));
      renderUi();
      break;
    case 'm': case 'M':
      ui_.home();
      renderUi();
      break;
    case 'o': case 'O':
      applyUiSelection();
      renderUi();
      break;
    case 'j': case 'J': rtc_.print(Serial); break;
    case 'r': case 'R': startBluetooth(); break;
    case 'f': case 'F': enterDfu(); break;
    case 'h': case 'H': case '?': printHelp(); break;
    case '\r': case '\n': case ' ': case '\t': break;
    default:
      Serial.print(F("Unknown command '"));
      Serial.print(command);
      Serial.println(F("'. Press h for help."));
      break;
  }
}

void WatchApplication::handleButton(size_t index, bool long_press) {
  Serial.print(F("Button "));
  Serial.print(index + 1);
  Serial.println(long_press ? F(" long press") : F(" short press"));
  if (model_.alarm_ringing) {
    if (index == 0 || (index == 3 && long_press)) {
      alarm_.dismiss(Serial);
    } else if (index == 3) {
      alarm_.snooze(Serial);
    }
    renderUi();
    return;
  }
  if (long_press && index == 0) {
    ui_.home();
    renderUi();
    return;
  }
  switch (index) {
    case 0: ui_.home(); break;
    case 1: applyUiAction(ui_.previous(model_)); break;
    case 2: applyUiAction(ui_.next(model_)); break;
    case 3: applyUiSelection(); break;
    default: return;
  }
  renderUi();
}

void WatchApplication::handleChord(bool backslash, bool long_press) {
  Serial.print(F("Diagonal "));
  Serial.print(backslash ? F("\\") : F("/"));
  Serial.println(long_press ? F(" hold") : F(" pinch"));
  if (model_.alarm_ringing) {
    if (long_press) {
      alarm_.dismiss(Serial);
    } else {
      alarm_.snooze(Serial);
    }
    renderUi();
    return;
  }
  if (backslash) {
    if (long_press) {
      applyUiSelection();
    } else {
      applyUiAction(ui_.previous(model_));
    }
  } else if (long_press) {
    ui_.home();
  } else {
    applyUiAction(ui_.next(model_));
  }
  renderUi();
}

void WatchApplication::pollButtons() {
  WatchButtonEvent event{};
  if (!buttons_.poll(event)) {
    return;
  }
  switch (event.type) {
    case WatchButtonEventType::Button:
      handleButton(event.button_index, event.long_press);
      break;
    case WatchButtonEventType::DiagonalBackslash:
      handleChord(true, event.long_press);
      break;
    case WatchButtonEventType::DiagonalSlash:
      handleChord(false, event.long_press);
      break;
    case WatchButtonEventType::HoldArmed:
      if (model_.vibration_on && !model_.alarm_ringing) {
        alarm_.pulseHaptic(120, 35);
      }
      break;
    default:
      break;
  }
}

void WatchApplication::setup() {
  configureGpio();
  i2c_.begin();

  // Let the nPM1100 finish port detection before attaching the USB console.
  delay(750);
  Serial.begin(115200);
  const uint32_t console_started = millis();
  while (!Serial && millis() - console_started < 3000) {
    delay(10);
  }

  printBanner();
  rtc_.begin(Serial);
  diagnostics_.runAll(Serial);
  activity_.begin(Serial);
  alarm_.begin();
  power_.begin(rtc_, Serial);
  printHelp();
  updateUiSensors();
  display_controller_.begin(Serial);
}

void WatchApplication::loop() {
  const uint8_t wake_events = power_.takeEvents();

  while (Serial.available() > 0) {
    handleSerialByte(static_cast<char>(Serial.read()));
  }
  ble_.poll();
  model_.bluetooth_connected = ble_.connected();
  processPhoneUpdates();
  if (alarm_.service(Serial) && display_controller_.active()) {
    renderUi();
  }
  if ((wake_events & WATCH_WAKE_ACCELEROMETER) != 0) {
    activity_.serviceInterrupt(Serial);
  }
  if ((wake_events & WATCH_WAKE_BATTERY) != 0) {
    updateUiSensors();
  }
  if ((wake_events & WATCH_WAKE_BUTTON) != 0 || buttons_.needsService()) {
    pollButtons();
  }
  if (display_controller_.clockMinuteChanged()) {
    renderUi();
  }

  // Button debouncing/holds and active actuator pulses need short deadlines.
  // Everything else sleeps until an interrupt or the one-second RTC safety
  // deadline; USB and STM32WB IPCC interrupts wake this sleep immediately.
  const uint32_t sleep_ms =
      (buttons_.needsService() || alarm_.needsFastService())
          ? 10U
          : 1000U;
  power_.sleepFor(sleep_ms);
}
