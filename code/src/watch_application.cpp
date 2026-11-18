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

WatchApplication* WatchApplication::instance_ = nullptr;

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
      ota_(flash_, ble_, model_),
      display_controller_(display_, ui_, model_, buttons_),
      diagnostics_(i2c_, accelerometer_, environment_, fuel_gauge_, display_,
                   buttons_, display_light_on_),
      activity_(accelerometer_, diagnostics_, rtc_, ble_, model_, config_),
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
  watchdog_.printStatus(Serial);
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
  Serial.println(F("  y  switch ordinary updates: quick 1-bpp (default) / GC fast"));
  Serial.println(F("  [/]  previous/next UI screen or item"));
  Serial.println(F("  m/o  UI home/select"));
  Serial.println(F("  j  print the STM32 RTC date/time"));
  Serial.println(F("  r  start BLE advertising for the phone (up to 10 seconds)"));
  Serial.println(F("  q  probe external flash (JEDEC ID), config store, update state"));
  Serial.println(F("  t  play the next notification alert tone (1-5)"));
  Serial.println(F("  n  show the next sample notification pop-up"));
  Serial.println(F("  p  forget the bonded phone (erase the stored link key)"));
  Serial.println(F("  f  reboot into resident STM32 USB DFU"));
  Serial.println(F("  %  hang on purpose (tests the watchdog and hang monitor)"));
  Serial.println(F("  z  print the sleep policy / power diagnostics"));
  Serial.println(F("  0  deep sleep OFF   1  deep sleep while Bluetooth is off (default)"));
  Serial.println(F("  2  deep sleep also with the radio on (experimental)"));
  Serial.println(F("  @  set local RTC time: @YYYY-MM-DDTHH:MM:SS then Enter"));
  Serial.println(F("  !  set alarm: !HH:MM then Enter"));
  Serial.println(F("  h  show this help"));
  Serial.println();
  Serial.println(F("Actuators only run on explicit v/b commands."));
  Serial.println(F("Pattern tests require x and end in booster-off standby."));
  Serial.println(F("Boot clears the panel and renders the clock automatically."));
  Serial.println(F("Physical corners: TL=2 next, TR=3 previous, BL=1 home, BR=4 select."));
  Serial.println(F("Home twice on the clock face: LED ring on/off."));
}

void WatchApplication::runVibrationTest() {
  Serial.println(F("Vibration motor: 300 ms at 50% drive"));
  analogWrite(BoardPins::VIBRATION, 128);
  delay(300);
  WatchAlarm::vibrationOff();
}

void WatchApplication::runBuzzerTest() {
  Serial.println(F("Buzzer: 2.4 kHz chirp"));
  tone(BoardPins::BUZZER, 2400, 180);
  delay(220);
  WatchAlarm::buzzerOff();
}

void WatchApplication::setDisplayLight(bool on) {
  display_light_on_ = on;
  model_.led_ring_on = on;
  display_light_since_ms_ = millis();
  digitalWrite(BoardPins::DISPLAY_LIGHT, on ? HIGH : LOW);
}

void WatchApplication::toggleDisplayLight() {
  setDisplayLight(!display_light_on_);
  Serial.print(F("Display LED ring: "));
  Serial.println(display_light_on_ ? F("ON") : F("OFF"));
}

// The front-light boost converter draws milliamps for as long as it is on,
// asleep or not: never leave it on unattended.
void WatchApplication::serviceDisplayLight() {
  if (!display_light_on_ ||
      millis() - display_light_since_ms_ < DISPLAY_LIGHT_TIMEOUT_MS) {
    return;
  }
  setDisplayLight(false);
  Serial.println(F("Display LED ring: auto-off."));
  if (display_controller_.active() && ui_.screen() == WatchScreen::Settings) {
    renderUi();  // the Settings list shows the switch
  }
}

void WatchApplication::setDeepSleepMode(DeepSleepMode mode) {
  power_.setMode(mode);
  Serial.print(F("Deep sleep (Stop 2): "));
  Serial.println(mode == DeepSleepMode::Off
                     ? F("OFF")
                     : mode == DeepSleepMode::RadioOffOnly
                           ? F("only while Bluetooth is off (default)")
                           : F("also with the radio on (experimental)"));
  if (!config_.savePowerMode(static_cast<uint8_t>(mode))) {
    Serial.println(F("[WARN] Not stored (no flash): lasts until the reset."));
  }
}

uint8_t WatchApplication::sleepBusy() {
  uint8_t busy = SLEEP_BUSY_NONE;
  if (alarm_.needsFastService()) {
    busy |= SLEEP_BUSY_ACTUATOR;
  }
  if (buttons_.needsService()) {
    busy |= SLEEP_BUSY_BUTTONS;
  }
  if (flash_.awake()) {
    busy |= SLEEP_BUSY_FLASH;
  }
  if (!ota_.idle()) {
    busy |= SLEEP_BUSY_OTA;
  }
  if (history_sending_ || ble_.sendPending() || ble_.transportBusy() ||
      ble_.rxPending()) {
    busy |= SLEEP_BUSY_LINK;
  }
  if (serial_capture_ || Serial.available() > 0) {
    busy |= SLEEP_BUSY_SERIAL;
  }
  return busy;
}

// Sleep until the next real deadline instead of on a fixed tick. Interrupts
// (buttons, sensors, USB, the radio) end the sleep early by themselves; this
// only has to cover what the loop does on its own clock. WatchPower caps the
// result (10 s in Stop for the watchdog, 1 s in Sleep mode).
uint32_t WatchApplication::sleepBudgetMs() {
  // Debounce, holds, actuator timing and bursts to the phone are polled.
  if (buttons_.needsService() || alarm_.needsFastService() ||
      history_sending_ || ble_.sendPending()) {
    return 10;
  }
  const uint32_t now = millis();
  uint32_t budget = 60000;
  const auto consider = [&budget](uint32_t ms) {
    if (ms < budget) {
      budget = ms;
    }
  };
  const auto remaining = [](uint32_t interval, uint32_t elapsed) -> uint32_t {
    return elapsed >= interval ? 0 : interval - elapsed;
  };
  if (!ota_.idle()) {
    consider(250);  // prompt / chunk time-outs and the reboot delay
  }
  consider(alarm_.msUntilNextEvent(now));
  if (peek_pending_) {
    return 1;
  }
  if (model_.notification_peek) {
    consider(remaining(NOTIFICATION_PEEK_MS, now - peek_since_ms_));
  }
  if (display_controller_.active() && ui_.screen() == WatchScreen::Clock &&
      model_.time_valid) {
    consider(rtc_.msUntilNextMinute());  // the only time-dependent screen
  }
  if (phone_session_ && ble_.connected() && !ota_.transferActive()) {
    consider(remaining(STATUS_INTERVAL_MS, now - last_status_ms_));
  }
  consider(activity_.msUntilNextService(now));
  consider(ble_.msUntilAdvertisingService());
  if (model_.find_phone_active) {
    consider(remaining(FIND_PHONE_RING_MS, now - find_phone_since_ms_));
  }
  if (display_light_on_) {
    consider(remaining(DISPLAY_LIGHT_TIMEOUT_MS,
                       now - display_light_since_ms_));
  }
  return budget == 0 ? 1 : budget;
}

// Runs with interrupts disabled right before the core stops: controller
// traffic that arrived after the loop's BLE.poll() must be served first.
bool WatchApplication::sleepVeto() {
  return instance_ != nullptr && instance_->ble_.rxPending();
}

void WatchApplication::enterDfu() {
  if (!dfu_.available()) {
    dfu_.request(Serial);
    return;
  }
  activity_.saveNow(Serial);
  display_.shutdown();
  display_light_on_ = false;
  digitalWrite(BoardPins::DISPLAY_LIGHT, LOW);
  WatchAlarm::vibrationOff();
  WatchAlarm::buzzerOff();
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

  // A forced BME280 conversion keeps the MCU awake for about 10 ms. The
  // clock face (redrawn every minute) only shows whole degrees, so measure
  // at most every few minutes unless a screen with live values is up.
  const WatchScreen screen = ui_.screen();
  const uint32_t now = millis();
  if (!model_.environment_valid ||
      now - environment_read_ms_ >= ENVIRONMENT_INTERVAL_MS) {
    environment_read_ms_ = now;
    EnvironmentReading environment{};
    model_.environment_valid =
        diagnostics_.environmentReady() && environment_.read(environment);
    if (model_.environment_valid) {
      model_.temperature_tenths_c =
          static_cast<int16_t>(lroundf(environment.temperature_c * 10.0f));
      model_.pressure_hpa =
          static_cast<uint16_t>(lroundf(environment.pressure_hpa));
      model_.pressure_pa =
          static_cast<uint32_t>(lroundf(environment.pressure_hpa * 100.0f));
      model_.humidity_percent =
          environment.has_humidity
              ? static_cast<uint8_t>(constrain(
                    lroundf(environment.humidity_percent), 0L, 100L))
              : 0;
    }
  }

  activity_.pollSteps();
  readBattery();
  updateMusicPosition();

  model_.led_ring_on = display_light_on_;
  model_.bluetooth_enabled = ble_.ready();
  model_.bluetooth_connected = ble_.connected();
  model_.secure_link_available = config_.available();
  model_.phone_bonded = ble_.bond().valid;
}

void WatchApplication::readBattery() {
  BatteryReading battery{};
  model_.battery_valid =
      diagnostics_.fuelGaugeReady() && fuel_gauge_.read(battery);
  if (model_.battery_valid) {
    model_.battery_percent = static_cast<uint8_t>(
        constrain(lroundf(battery.state_of_charge_percent), 0L, 100L));
    model_.battery_millivolts =
        static_cast<uint16_t>(lroundf(battery.voltage_v * 1000.0f));
    ble_.updateBattery(model_.battery_percent);
  }
}

void WatchApplication::updateMusicPosition() {
  uint32_t position = music_base_position_;
  if (model_.music_state == MusicState::Playing) {
    position += (millis() - music_base_ms_) / 1000;
  }
  if (model_.music_duration_seconds != 0 &&
      position > model_.music_duration_seconds) {
    position = model_.music_duration_seconds;
  }
  model_.music_position_seconds = position;
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
  // BLE.begin() cannot be fed from inside and spins forever if CPU2 never
  // answers; give it the full watchdog window so a hang resets the watch.
  WatchWatchdog::feed();
  // The BLE controller times its sleep from the "RF wake-up" clock, which
  // the generic WB5MMG clock setup leaves unselected.
  WatchPower::selectRadioWakeClock();
  // The secure-link setting is sampled here: it takes effect when the radio
  // starts, which is why changing it restarts a running radio.
  const bool started_ok =
      ble_.begin(Serial, model_.secure_link, helloFlags());
  WatchWatchdog::feed();
  rtc_.restoreAfterBackupReset(preserved_epoch, millis() - started, Serial);
  model_.bluetooth_connected = ble_.connected();
  return started_ok;
}

void WatchApplication::rememberBluetooth(bool on) {
  if (!config_.saveRadioOn(on) && flash_.present()) {
    Serial.println(F("[WARN] Bluetooth state could not be saved to flash."));
  }
}

// Boot: switch the radio back on if it was on when the watch last ran. Not
// after a watchdog reset: if starting the radio is what hung, doing it again
// on every boot would reset the watch in a loop. The stored state is left
// alone, so the next clean boot tries again.
void WatchApplication::restoreBluetooth() {
  bool was_on = false;
  if (!config_.loadRadioOn(was_on) || !was_on) {
    return;
  }
  if (watchdog_.resetByWatchdog()) {
    Serial.println(
        F("[WARN] Bluetooth was on but is left off after a watchdog reset."));
    return;
  }
  Serial.println(F("Bluetooth was on before the restart; switching it on."));
  startBluetooth();
}

void WatchApplication::restartBluetooth() {
  if (!ble_.ready()) {
    return;
  }
  ble_.end(Serial);
  startBluetooth();
}

uint8_t WatchApplication::helloFlags() const {
  uint8_t flags = 0;
  if (model_.secure_link) {
    flags |= HELLO_FLAG_SECURE;
  }
  if (ota_.supported()) {
    flags |= HELLO_FLAG_OTA;
  }
  if (config_.available()) {
    flags |= HELLO_FLAG_STORAGE;
  }
  return flags;
}

void WatchApplication::loadStoredSettings() {
  StoredSettings settings{};
  if (config_.loadSettings(settings)) {
    model_.vibration_on = settings.vibration;
    model_.quiet_mode = settings.quiet;
    model_.alert_tone = settings.tone;
    model_.secure_link = settings.secure_link;
    Serial.println(F("Settings restored from flash."));
  }
  model_.secure_link_available = config_.available();
  StoredBond bond{};
  if (config_.loadBond(bond) && bond.valid) {
    ble_.setBond(bond);
  }
  model_.phone_bonded = ble_.bond().valid;
}

void WatchApplication::persistSettings() {
  StoredSettings settings{};
  settings.vibration = model_.vibration_on;
  settings.quiet = model_.quiet_mode;
  settings.tone = model_.alert_tone;
  settings.secure_link = model_.secure_link;
  if (!config_.saveSettings(settings) && flash_.present()) {
    Serial.println(F("[WARN] Settings could not be saved to flash."));
  }
  model_.secure_link_available = config_.available();
}

void WatchApplication::forgetPhone() {
  if (!config_.eraseBond() && flash_.present()) {
    Serial.println(F("[WARN] Stored link key could not be erased."));
  }
  ble_.setBond(StoredBond{});
  model_.phone_bonded = false;
  Serial.println(F("Bonded phone forgotten. Remove the bond on the phone too."));
}

void WatchApplication::printStorageStatus() {
  flash_.printStatus(Serial);
  config_.printStatus(Serial);
  ota_.printStatus(Serial);
  Serial.print(F("Secure link: "));
  Serial.print(model_.secure_link ? F("ON") : F("OFF"));
  Serial.print(F(", phone bonded: "));
  Serial.print(ble_.bond().valid ? F("yes") : F("no"));
  Serial.print(F(", link encrypted: "));
  Serial.println(ble_.encrypted() ? F("yes") : F("no"));
}

void WatchApplication::displayWaitHook() {
  WatchWatchdog::feed();
  backgroundHook();
}

void WatchApplication::backgroundHook() {
  if (instance_ != nullptr) {
    instance_->backgroundService();
  }
}

// Runs from inside blocking work (e-paper BUSY waits, the update read-back).
// It may only do things that cannot start another blocking operation:
// receive/acknowledge/queue BLE traffic and advance actuator timing. Phone
// updates are NOT applied here and nothing here renders.
void WatchApplication::backgroundService() {
  if (in_background_) {
    return;
  }
  const uint32_t now = millis();
  if (now - last_background_ms_ < 20) {
    return;
  }
  last_background_ms_ = now;
  in_background_ = true;
  ble_.poll();
  alarm_.serviceOutput();
  in_background_ = false;
}

void WatchApplication::otaDataThunk(const uint8_t* data, size_t length) {
  if (instance_ != nullptr) {
    instance_->ota_.handleData(data, length);
  }
}

// Reset into the normal boot path so the resident bootloader installs the
// staged image. The DFU-request backup register is deliberately NOT set.
void WatchApplication::rebootForUpdate() {
  Serial.println(F("Rebooting to install the staged firmware update..."));
  activity_.saveNow(Serial);
  display_.shutdown();
  display_light_on_ = false;
  digitalWrite(BoardPins::DISPLAY_LIGHT, LOW);
  WatchAlarm::vibrationOff();
  WatchAlarm::buzzerOff();
  ble_.flush();
  if (ble_.ready()) {
    ble_.end(Serial);
  }
  // The bootloader only waits about 20 ms for the chip: leave it awake and
  // make sure no erase or program is still in flight.
  if (!flash_.prepareForReset(3000)) {
    Serial.println(F("[WARN] External flash still busy before reset."));
  }
  Serial.flush();
  delay(25);
  NVIC_SystemReset();
  while (true) {
  }
}

void WatchApplication::applyUiAction(WatchUiAction action) {
  switch (action) {
    case WatchUiAction::TimerChanged:
      alarm_.timerChanged();
      break;
    case WatchUiAction::NotificationDismissed:
      dismissShownNotification();
      break;
    case WatchUiAction::MediaPrevious:
      ble_.send("MEDIA|PREV");
      break;
    case WatchUiAction::MediaPlayPause:
      ble_.send("MEDIA|PLAY_PAUSE");
      // Optimistic local toggle; the phone confirms with MUSIC on change.
      updateMusicPosition();
      if (model_.music_state != MusicState::None) {
        music_base_position_ = model_.music_position_seconds;
        music_base_ms_ = millis();
        model_.music_state = model_.music_state == MusicState::Playing
                                 ? MusicState::Paused
                                 : MusicState::Playing;
      }
      break;
    case WatchUiAction::MediaNext:
      ble_.send("MEDIA|NEXT");
      break;
    case WatchUiAction::BluetoothToggle:
      if (ble_.ready()) {
        ble_.end(Serial);
        rememberBluetooth(false);
      } else if (startBluetooth()) {
        rememberBluetooth(true);
      }
      break;
    case WatchUiAction::PhoneSettingsChanged:
      if (model_.quiet_mode) {
        alarm_.stopCallAlert();
      }
      persistSettings();
      sendSettings();
      break;
    case WatchUiAction::AlertToneChanged:
      persistSettings();
      sendSettings();
      // Preview the new choice even in quiet mode: the wearer asked for it.
      alarm_.notifyAlert(false, model_.alert_tone);
      break;
    case WatchUiAction::SecureLinkChanged:
      if (!model_.secure_link) {
        // Turning the setting off erases the stored key (PROTOCOL.md).
        forgetPhone();
      }
      persistSettings();
      if (model_.secure_link && !config_.available()) {
        // Never run "secure" on a setting or key that cannot be stored.
        model_.secure_link = false;
        Serial.println(F("[WARN] Secure link needs working flash: left OFF."));
      }
      Serial.print(F("Secure link "));
      Serial.println(model_.secure_link ? F("ON") : F("OFF"));
      restartBluetooth();
      break;
    case WatchUiAction::ForgetPhone:
      forgetPhone();
      if (ble_.secure()) {
        // Drop the session that was encrypted with the erased key.
        restartBluetooth();
      }
      break;
    case WatchUiAction::CameraOpen:
      if (ble_.send("CAMERA|OPEN")) {
        model_.camera_state = CameraState::Opening;
        model_.camera_number = 0;
      } else {
        model_.camera_state = CameraState::Closed;
        Serial.println(F("Camera: no phone connected."));
      }
      break;
    case WatchUiAction::CameraShutter:
      if (ble_.sendf("CAMERA|SHUTTER|%u", model_.camera_timer_seconds)) {
        Serial.print(F("Camera: shutter, timer "));
        Serial.println(model_.camera_timer_seconds);
      }
      break;
    case WatchUiAction::FindPhoneToggle: {
      const bool start = !model_.find_phone_active;
      if (ble_.sendf("FIND_PHONE|%d", start ? 1 : 0)) {
        model_.find_phone_active = start;
        find_phone_since_ms_ = millis();
      } else {
        Serial.println(F("Find phone: no phone connected."));
      }
      break;
    }
    default:
      break;
  }

  if (model_.led_ring_on != display_light_on_) {
    setDisplayLight(model_.led_ring_on);
  }
}

void WatchApplication::applyUiSelection() {
  applyUiAction(ui_.select(model_));
}

void WatchApplication::storeNotification(uint16_t id, const char* app,
                                         const char* title,
                                         const char* text) {
  WatchNotification* list = model_.notifications;
  uint8_t count = model_.notification_count;
  // New arrivals are not redrawn (see applyPhoneUpdate), so the paging index
  // must keep pointing at the notification that is on screen; otherwise the
  // next select would dismiss one the user has not seen.
  // When Messages is not open, a new arrival simply becomes the first shown.
  const bool viewing =
      ui_.screen() == WatchScreen::Notifications && ui_.isInside();
  if (!viewing) {
    ui_.setNotificationIndex(0);
  }
  const uint8_t shown_index = ui_.notificationIndex();
  const bool track_shown = viewing && shown_index < count;
  const uint16_t shown_id = track_shown ? list[shown_index].id : 0;
  const bool shown_replaced = track_shown && shown_id == id;
  for (uint8_t index = 0; index < count; ++index) {
    if (list[index].id == id) {
      memmove(&list[index], &list[index + 1],
              sizeof(WatchNotification) * (count - index - 1));
      --count;
      break;
    }
  }
  if (count >= WatchUiModel::MAX_NOTIFICATIONS) {
    count = WatchUiModel::MAX_NOTIFICATIONS - 1;  // drop the oldest
  }
  memmove(&list[1], &list[0], sizeof(WatchNotification) * count);
  list[0].id = id;
  copyPhoneText(list[0].app, app);
  copyPhoneText(list[0].title, title);
  copyPhoneText(list[0].text, text);
  model_.notification_count = static_cast<uint8_t>(count + 1);

  if (track_shown && !shown_replaced) {
    for (uint8_t index = 1; index < model_.notification_count; ++index) {
      if (list[index].id == shown_id) {
        ui_.setNotificationIndex(index);
        break;
      }
    }
  }
  ui_.clampNotificationIndex(model_.notification_count);
}

bool WatchApplication::removeNotification(uint16_t id) {
  WatchNotification* list = model_.notifications;
  const uint8_t count = model_.notification_count;
  for (uint8_t index = 0; index < count; ++index) {
    if (list[index].id == id) {
      memmove(&list[index], &list[index + 1],
              sizeof(WatchNotification) * (count - index - 1));
      model_.notification_count = static_cast<uint8_t>(count - 1);
      ui_.clampNotificationIndex(model_.notification_count);
      return true;
    }
  }
  return false;
}

void WatchApplication::dismissShownNotification() {
  const uint8_t index = ui_.notificationIndex();
  if (index >= model_.notification_count) {
    return;
  }
  const uint16_t id = model_.notifications[index].id;
  removeNotification(id);
  // id 0 is a local Sleep as Android message with no phone counterpart.
  if (id != 0) {
    ble_.sendf("NOTIFY_DISMISS|%u", id);
  }
}

void WatchApplication::sendStatus() {
  if (!ble_.connected()) {
    return;
  }
  activity_.pollSteps();
  readBattery();
  // The board has no charger-status input wired to the MCU: -1 = unknown.
  const bool sent = ble_.sendf(
      "STATUS|%d|%u|%lu|-1",
      model_.battery_valid ? static_cast<int>(model_.battery_percent) : -1,
      model_.battery_valid ? static_cast<unsigned>(model_.battery_millivolts)
                           : 0U,
      static_cast<unsigned long>(model_.steps));
  if (sent) {
    last_status_ms_ = millis();
    last_status_battery_ =
        model_.battery_valid ? static_cast<int16_t>(model_.battery_percent)
                             : -1;
  }
}

void WatchApplication::sendSettings() {
  ble_.sendf("SETTINGS|%d|%d|%u", model_.vibration_on ? 1 : 0,
             model_.quiet_mode ? 1 : 0,
             static_cast<unsigned>(model_.alert_tone));
}

void WatchApplication::serviceHistory() {
  // Leave queue room for ACK/NAK replies while the burst drains.
  while (history_sending_ && ble_.outboundFree() > 2) {
    if (history_next_ >= activity_.historyCount()) {
      ble_.send("HISTORY_END");
      history_sending_ = false;
      break;
    }
    uint32_t date = 0;
    uint32_t steps = 0;
    if (activity_.historyEntry(history_next_, date, steps)) {
      ble_.sendf("HISTORY|%lu|%lu", static_cast<unsigned long>(date),
                 static_cast<unsigned long>(steps));
    }
    ++history_next_;
  }
}

void WatchApplication::servicePhoneLink() {
  if (ble_.takeDisconnected()) {
    Serial.println(F("Phone disconnected."));
    phone_session_ = false;
    history_sending_ = false;
    model_.find_phone_active = false;
    model_.phone_status_valid = false;
    bool redraw = ota_.onDisconnect(Serial);
    model_.camera_state = CameraState::Closed;
    // These apps show the link state.
    redraw |= ui_.screen() == WatchScreen::FindPhone ||
              ui_.screen() == WatchScreen::Camera;
    if (model_.call_active) {
      endCall();
      redraw = true;
    }
    if (redraw && display_controller_.active()) {
      renderUi();
    }
  }
  if (!phone_session_ || !ble_.connected()) {
    return;
  }
  if (ota_.transferActive()) {
    return;  // STATUS and HISTORY are suspended during a firmware transfer
  }
  serviceHistory();
  const int16_t battery =
      model_.battery_valid ? static_cast<int16_t>(model_.battery_percent) : -1;
  if (battery != last_status_battery_ ||
      millis() - last_status_ms_ >= STATUS_INTERVAL_MS) {
    sendStatus();
  }
}

void WatchApplication::endCall() {
  model_.call_active = false;
  alarm_.stopCallAlert();
}

bool WatchApplication::handleCallButton(bool select, bool back) {
  if (select) {
    ble_.send("CALL_REJECT");
    endCall();
    Serial.println(F("Call rejected."));
    return true;
  }
  if (back) {
    // Hide the call screen locally; the phone keeps ringing.
    endCall();
    return true;
  }
  alarm_.stopCallAlert();  // any other button just silences the watch
  return false;
}

bool WatchApplication::applyPhoneUpdate(const PhoneUpdate& update) {
  const int32_t* n = update.number;
  const WatchScreen screen = ui_.screen();
  switch (update.type) {
    case PhoneUpdateType::Hello:
      Serial.print(F("Phone HELLO, protocol "));
      Serial.println(n[0]);
      phone_session_ = true;
      history_sending_ = false;
      model_.find_phone_active = false;
      sendStatus();
      ota_.sendBootReport(Serial);
      model_.camera_state = CameraState::Closed;
      // These apps show the link state.
      return screen == WatchScreen::FindPhone || screen == WatchScreen::Camera;
    case PhoneUpdateType::Time:
      if (rtc_.setUtc(update.utc, n[0])) {
        rtc_.setTransition(static_cast<uint32_t>(n[2]), n[1]);
        rtc_.updateModel(model_);
        Serial.print(F("BLE time synchronization applied, UTC offset "));
        Serial.println(n[0]);
      }
      return screen == WatchScreen::Clock;
    case PhoneUpdateType::Notify:
      storeNotification(static_cast<uint16_t>(n[0]), update.text_a,
                        update.text_b, update.text_c);
      if (n[1] == 1) {
        // New arrival: vibration follows its own setting, quiet mode only
        // silences the tone.
        alarm_.notifyAlert(model_.vibration_on,
                           model_.quiet_mode ? 0 : model_.alert_tone);
        Serial.print(F("Notification alert: "));
        Serial.print(update.text_a);
        Serial.print(F(" / "));
        Serial.println(update.text_b);
        // Show it for a few seconds, unless the Messages card is already
        // open. The loop draws it once this batch of updates is done, so
        // no e-paper refresh starts from inside BLE processing.
        if (!(screen == WatchScreen::Notifications && ui_.isInside())) {
          peek_pending_ = true;
        }
      }
      return false;
    case PhoneUpdateType::NotifyClear:
      if (n[0] == 0) {
        model_.notification_count = 0;
        ui_.clampNotificationIndex(0);
        return screen == WatchScreen::Notifications;
      }
      return removeNotification(static_cast<uint16_t>(n[0])) &&
             screen == WatchScreen::Notifications;
    case PhoneUpdateType::Call:
      if (n[0] == 1) {
        // Calls are the one phone event that deliberately takes the screen.
        model_.call_active = true;
        copyPhoneText(model_.call_caller, update.text_a);
        if (!model_.quiet_mode) {
          alarm_.startCallAlert();
        }
        Serial.println(F("Incoming call."));
        return true;
      }
      if (model_.call_active) {
        endCall();
        return true;
      }
      return false;
    case PhoneUpdateType::Weather:
      model_.phone_weather_valid = true;
      model_.forecast_temperature_c = static_cast<int16_t>(n[0]);
      model_.forecast_high_c = static_cast<int8_t>(n[1]);
      model_.forecast_low_c = static_cast<int8_t>(n[2]);
      model_.weather_code = static_cast<int16_t>(n[3]);
      copyPhoneText(model_.weather_condition, update.text_a);
      // The clock face shows an altitude worked out from this.
      model_.sea_level_pressure_tenths_hpa =
          (update.utc >= 8000 && update.utc <= 11000)
              ? static_cast<uint16_t>(update.utc)
              : 0;
      return screen == WatchScreen::Clock;
    case PhoneUpdateType::Music:
      model_.music_state = static_cast<MusicState>(n[0]);
      music_base_position_ = static_cast<uint32_t>(n[1]);
      music_base_ms_ = millis();
      model_.music_duration_seconds = static_cast<uint32_t>(n[2]);
      copyPhoneText(model_.music_title, update.text_a);
      copyPhoneText(model_.music_artist, update.text_b);
      updateMusicPosition();
      return screen == WatchScreen::Music;
    case PhoneUpdateType::NextAlarm:
      model_.next_alarm_utc = update.utc;
      return screen == WatchScreen::Alarms && ui_.isInside();
    case PhoneUpdateType::Event:
      model_.event_utc = update.utc;
      copyPhoneText(model_.event_title, update.text_a);
      return false;
    case PhoneUpdateType::Phone:
      model_.phone_status_valid = true;
      model_.phone_battery_percent = static_cast<uint8_t>(n[0]);
      model_.phone_charging = n[1] != 0;
      model_.phone_dnd = n[2] != 0;
      return false;
    case PhoneUpdateType::Settings:
      model_.vibration_on = n[0] != 0;
      model_.quiet_mode = n[1] != 0;
      if (n[2] >= 0) {
        model_.alert_tone = static_cast<uint8_t>(n[2]);
      }
      if (model_.quiet_mode) {
        alarm_.stopCallAlert();
      }
      persistSettings();  // writes flash only if something changed
      return screen == WatchScreen::Settings && ui_.isInside();
    case PhoneUpdateType::TonePreview:
      alarm_.notifyAlert(false, static_cast<uint8_t>(n[0]));
      return false;
    case PhoneUpdateType::OtaBegin:
      return ota_.handleBegin(static_cast<uint32_t>(n[0]), update.utc,
                              update.text_a,
                              model_.alarm_ringing || model_.call_active,
                              Serial);
    case PhoneUpdateType::OtaEnd:
      if (ota_.handleEnd(Serial)) {
        // The read-back takes several seconds: show why first. BLE stays
        // serviced through the wait hook / idle hook meanwhile.
        if (display_controller_.active()) {
          renderUi();
        }
        ota_.verify(Serial);
      }
      return true;
    case PhoneUpdateType::OtaAbort:
      return ota_.handleAbort(Serial);
    case PhoneUpdateType::FindWatch:
      alarm_.startFindAlert(Serial);
      return false;
    case PhoneUpdateType::Camera:
      model_.camera_state = static_cast<CameraState>(n[0]);
      model_.camera_number = static_cast<uint16_t>(n[1]);
      copyPhoneText(model_.camera_detail, update.text_a);
      Serial.print(F("Camera: phone state "));
      Serial.println(n[0]);
      return screen == WatchScreen::Camera && ui_.isInside();
    case PhoneUpdateType::RequestStatus:
      if (!ota_.transferActive()) {
        sendStatus();
      }
      return false;
    case PhoneUpdateType::RequestHistory:
      if (ota_.transferActive()) {
        return false;
      }
      history_sending_ = true;
      history_next_ = 0;
      serviceHistory();
      return false;
    case PhoneUpdateType::StepsReset:
      activity_.resetSteps(Serial);
      return screen == WatchScreen::Activity;
    case PhoneUpdateType::SleepStart:
      activity_.startSleepTracking(Serial);
      return false;
    case PhoneUpdateType::SleepStop:
      activity_.stopSleepTracking(Serial);
      return false;
    case PhoneUpdateType::SleepPause:
      activity_.pauseSleepTracking(n[0], Serial);
      return false;
    case PhoneUpdateType::SleepAlarm:
      alarm_.scheduleSleepAlarm(n[0], Serial);
      return false;
    case PhoneUpdateType::SleepAlarmStop:
      return alarm_.stopSleepAlarm(Serial);
    case PhoneUpdateType::SleepNotify:
      storeNotification(0, "SLEEP AS ANDROID", update.text_a, update.text_b);
      return false;
    case PhoneUpdateType::SleepHint: {
      const int32_t repeats = n[0] < 5 ? n[0] : 5;
      for (int32_t i = 0; i < repeats; ++i) {
        analogWrite(BoardPins::VIBRATION, 128);
        delay(80);
        analogWrite(BoardPins::VIBRATION, 0);
        delay(80);
      }
      if (repeats > 0) {
        WatchAlarm::vibrationOff();
      }
      return false;
    }
    default:
      return false;
  }
}

// Draws a newly alerted notification over the current screen, and draws the
// screen underneath again once NOTIFICATION_PEEK_MS has passed.
void WatchApplication::serviceNotificationPeek() {
  if (peek_pending_) {
    peek_pending_ = false;
    if (display_controller_.active() && model_.notification_count > 0 &&
        !model_.call_active && !model_.alarm_ringing &&
        !ota_.screenShown()) {
      model_.notification_peek = true;
      renderUi();
      // Counted from when the refresh finished, so it is on screen for the
      // whole time.
      peek_since_ms_ = millis();
    }
    return;
  }
  if (model_.notification_peek &&
      millis() - peek_since_ms_ >= NOTIFICATION_PEEK_MS) {
    endNotificationPeek();
  }
}

void WatchApplication::serviceFindPhone() {
  if (!model_.find_phone_active ||
      millis() - find_phone_since_ms_ < FIND_PHONE_RING_MS) {
    return;
  }
  model_.find_phone_active = false;
  Serial.println(F("Find phone: phone has stopped ringing by now."));
  if (display_controller_.active() && !model_.notification_peek &&
      (ui_.screen() == WatchScreen::FindPhone)) {
    renderUi();
  }
}

// Returns whether a peek was showing.
bool WatchApplication::endNotificationPeek() {
  if (!model_.notification_peek) {
    return false;
  }
  model_.notification_peek = false;
  if (display_controller_.active()) {
    renderUi();
  }
  return true;
}

void WatchApplication::processPhoneUpdates() {
  bool should_render = false;
  PhoneUpdate update{};
  // Bounded by the inbound queue size; render once for the whole batch.
  for (uint8_t processed = 0;
       processed < WatchBle::INBOUND_CAPACITY && ble_.takeUpdate(update);
       ++processed) {
    should_render |= applyPhoneUpdate(update);
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
    case 'y': case 'Y':
      display_controller_.setKwMode(!display_controller_.kwMode());
      Serial.println(display_controller_.kwMode()
                         ? F("Quick 1-bpp display update: ON")
                         : F("Quick 1-bpp display update: OFF (GC fast)"));
      renderUi();
      break;
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
    case 'r': case 'R':
      if (startBluetooth()) {
        rememberBluetooth(true);
      }
      break;
    case '%':
      // Deliberate hang: checks that the watchdog resets the watch and that
      // the hang monitor reports this loop on the next boot.
      Serial.println(F("Hanging on purpose; the watchdog resets in 20 s."));
      Serial.flush();
      for (;;) {
      }
    case 'q': case 'Q': printStorageStatus(); break;
    case 't': case 'T':
      preview_tone_ = static_cast<uint8_t>(
          preview_tone_ % WatchAlarm::TONE_COUNT + 1);
      Serial.print(F("Alert tone "));
      Serial.println(preview_tone_);
      alarm_.notifyAlert(false, preview_tone_);
      break;
    case 'p': case 'P':
      forgetPhone();
      if (ble_.secure()) {
        restartBluetooth();
      }
      break;
    case 'f': case 'F': enterDfu(); break;
    case 'z': case 'Z':
      power_.printStatus(Serial, ble_.ready(), sleepBusy());
      Serial.print(F("Next loop deadline in ms: "));
      Serial.println(sleepBudgetMs());
      Serial.print(F("Advertising: "));
      Serial.println(!ble_.ready() ? F("radio off")
                                   : ble_.connected()
                                         ? F("connected")
                                         : ble_.advertisingFast()
                                               ? F("fast (100 ms)")
                                               : F("slow (1 s)"));
      Serial.print(F("MAX17048 forced hibernate: "));
      Serial.println(fuel_gauge_.hibernating() ? F("yes") : F("no"));
      break;
    case '0': setDeepSleepMode(DeepSleepMode::Off); break;
    case '1': setDeepSleepMode(DeepSleepMode::RadioOffOnly); break;
    case '2': setDeepSleepMode(DeepSleepMode::Always); break;
    case 'n': case 'N': {
      // Shows the next sample notification as a pop-up (no phone needed).
      static constexpr const char* SAMPLES[][3] = {
          {"Messages", "Alexandra Montgomery-Smith",
           "Running about ten minutes late, save me a seat please!"},
          {"Gmail", "Your order has shipped", "Arriving Thursday"},
          {"Calendar", "", "Team stand-up in 10 minutes, room 4.02"},
          {"WhatsApp", "Mum", "ok"},
      };
      static uint8_t sample = 0;
      const char* const* chosen = SAMPLES[sample];
      sample = static_cast<uint8_t>((sample + 1) % 4);
      storeNotification(0, chosen[0], chosen[1], chosen[2]);
      peek_pending_ = true;
      Serial.print(F("Sample notification: "));
      Serial.println(chosen[0]);
      break;
    }
    case 'h': case 'H': case '?': printHelp(); break;
    case '\r': case '\n': case ' ': case '\t': break;
    default:
      Serial.print(F("Unknown command '"));
      Serial.print(command);
      Serial.println(F("'. Press h for help."));
      break;
  }
}

void WatchApplication::handleButton(size_t index) {
  Serial.print(F("Button "));
  Serial.print(index + 1);
  Serial.println(F(" press"));
  // Only two home presses in a row on the clock face count as a double press.
  const bool second_home_press =
      index == 0 && home_press_armed_ &&
      millis() - home_press_ms_ <= DOUBLE_PRESS_MS;
  home_press_armed_ = false;
  if (alarm_.stopFindAlert()) {
    Serial.println(F("Find my watch: stopped by button."));
    return;
  }
  // Any button just closes a notification that is being shown briefly.
  if (endNotificationPeek()) {
    return;
  }
  if (model_.alarm_ringing) {
    if (index == 0) {
      alarm_.dismiss(Serial);
    } else if (index == 3) {
      alarm_.snooze(Serial);
    }
    renderUi();
    return;
  }
  if (model_.call_active) {
    if (handleCallButton(index == 3, index == 0)) {
      renderUi();
    }
    return;
  }
  if (ota_.screenShown()) {
    // Select accepts and back declines the update prompt; nothing else is
    // navigable while the update overlay is up.
    if (ota_.handleButton(index == 3, index == 0, Serial)) {
      renderUi();
    }
    return;
  }
  if (index == 0 && ui_.screen() == WatchScreen::Clock) {
    // Home does nothing on the clock face, so nothing is redrawn: a refresh
    // would block the buttons for most of the double-press window.
    if (second_home_press) {
      toggleDisplayLight();
    } else {
      home_press_armed_ = true;
      home_press_ms_ = millis();
    }
    return;
  }
  if (index == 0 && ui_.screen() == WatchScreen::FindPhone && ui_.isInside() &&
      model_.find_phone_active) {
    applyUiAction(WatchUiAction::FindPhoneToggle);  // leaving stops the ring
  }
  if (index == 0 && ui_.screen() == WatchScreen::Camera && ui_.isInside() &&
      model_.camera_state != CameraState::Closed) {
    ble_.send("CAMERA|CLOSE");  // leaving closes the phone's camera too
    model_.camera_state = CameraState::Closed;
  }
  if (index == 3 && ui_.screen() == WatchScreen::Clock &&
      model_.music_state != MusicState::None) {
    // Play/pause from the clock: the face does not show music, so nothing
    // needs redrawing.
    applyUiSelection();
    Serial.println(F("Clock: music play/pause."));
    return;
  }
  switch (index) {
    case 0: ui_.home(); break;
    case 1: applyUiAction(ui_.next(model_)); break;
    case 2: applyUiAction(ui_.previous(model_)); break;
    case 3: applyUiSelection(); break;
    default: return;
  }
  renderUi();
}

void WatchApplication::pollButtons() {
  WatchButtonEvent event{};
  if (!buttons_.poll(event)) {
    return;
  }
  handleButton(event.button_index);
}

void WatchApplication::setup() {
  configureGpio();
  watchdog_.begin();
  // Take the external flash pins early (the bootloader leaves them analog)
  // so its chip select is never left floating; reported further down.
  flash_.begin();
  instance_ = this;
  display_.setWaitHook(displayWaitHook);
  display_.setIdleBusPins(BoardPins::DISPLAY_SCK, BoardPins::DISPLAY_MOSI);
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
  WatchWatchdog::feed();
  // External flash: non-destructive probe, then settings/bond/steps and any
  // firmware-update result. Everything below works without it (RAM only).
  flash_.printStatus(Serial);
  config_.begin(flash_, Serial);
  loadStoredSettings();
  ota_.begin(Serial);
  ota_.setIdleHook(backgroundHook);
  ble_.setOtaDataHandler(otaDataThunk);
  WatchWatchdog::feed();
  diagnostics_.runAll(Serial);
  WatchWatchdog::feed();
  activity_.begin(Serial);
  alarm_.begin();
  power_.begin(rtc_, watchdog_.resetByWatchdog(), Serial);
  power_.setWakeVeto(sleepVeto);
  uint8_t stored_sleep_mode = 0;
  if (config_.loadPowerMode(stored_sleep_mode)) {
    power_.setMode(static_cast<DeepSleepMode>(stored_sleep_mode));
  }
  printHelp();
  updateUiSensors();
  WatchWatchdog::feed();
  display_controller_.begin(Serial);
  restoreBluetooth();
}

void WatchApplication::loop() {
  WatchWatchdog::feed();
  watchdog_.service();
  if (i2c_.recoverIfStormed()) {
    Serial.println(F("[WARN] I2C interrupt storm broken; bus restarted."));
  }
  const uint8_t wake_events = power_.takeEvents();

  while (Serial.available() > 0) {
    // Someone is at the console: stay in USB-safe Sleep mode for a while
    // whatever the VDDUSB detection says.
    power_.noteActivity(WatchPower::CONSOLE_HOLD_MS);
    handleSerialByte(static_cast<char>(Serial.read()));
  }
  ble_.poll();
  ble_.serviceAdvertising();
  model_.bluetooth_enabled = ble_.ready();
  model_.bluetooth_connected = ble_.connected();
  processPhoneUpdates();
  serviceNotificationPeek();
  serviceFindPhone();
  servicePhoneLink();
  if (ble_.takeBondChange()) {
    // A phone has just paired: keep its key (flash work stays out of the
    // BLE callback that reported it).
    model_.phone_bonded = true;
    Serial.println(F("Phone paired; storing the link key."));
    if (!config_.saveBond(ble_.bond())) {
      Serial.println(F("[WARN] Link key not saved; it is lost at reset."));
    }
  }
  if (ota_.service(Serial) && display_controller_.active()) {
    renderUi();
  }
  if (ota_.rebootDue()) {
    rebootForUpdate();
  }
  if (alarm_.service(Serial) && display_controller_.active()) {
    renderUi();
  }
  if (rtc_.serviceTransition()) {
    Serial.println(F("UTC offset change (daylight saving) applied."));
    if (display_controller_.active()) {
      renderUi();
    }
  }
  if (model_.time_valid) {
    // Rolls the step history at local midnight.
    activity_.serviceDay(model_.local_epoch / 86400UL, rtc_.timeTrusted(),
                         Serial);
  }
  activity_.serviceStorage(Serial);
  if ((wake_events & WATCH_WAKE_ACCELEROMETER) != 0) {
    activity_.serviceInterrupt(Serial);
  }
  if ((wake_events & WATCH_WAKE_BATTERY) != 0) {
    updateUiSensors();
  }
  if ((wake_events & WATCH_WAKE_BUTTON) != 0 || buttons_.needsService()) {
    pollButtons();
    if (buttons_.needsService()) {
      // A button really is down (the display SPI borrowing PB4 also raises
      // the button interrupt, with nothing pressed). Full clocks for a while
      // after any press: snappy follow-up presses, and a way to get the USB
      // console up if VDDUSB detection ever fails.
      power_.noteActivity(WatchPower::BUTTON_HOLD_MS);
      display_light_since_ms_ = millis();
    }
  }
  serviceDisplayLight();
  if (display_controller_.clockMinuteChanged()) {
    renderUi();
  }
  // Send anything queued by button handling or alarms before sleeping.
  ble_.flush();

  // Sleep until the next deadline or interrupt. WatchPower chooses Stop 2 or
  // plain Sleep mode from the radio state and the busy flags.
  const uint32_t budget_ms = sleepBudgetMs();
  power_.sleep(budget_ms, ble_.ready(), sleepBusy());
}
