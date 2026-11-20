#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

#include <ET011TJ1.h>

#include "i2c_devices.h"
#include "watch_activity.h"
#include "watch_alarm.h"
#include "watch_ble.h"
#include "watch_buttons.h"
#include "watch_config.h"
#include "watch_diagnostics.h"
#include "watch_display.h"
#include "watch_power.h"
#include "watch_power_log.h"
#include "watch_rtc.h"
#include "watch_dfu.h"
#include "watch_flash.h"
#include "watch_ota.h"
#include "watch_ui.h"
#include "watch_watchdog.h"

class WatchApplication {
 public:
  WatchApplication();

  void setup();
  void loop();

 private:
  void configureGpio();
  void printBanner();
  void printHelp();
  void runVibrationTest();
  void runBuzzerTest();
  void toggleDisplayLight();
  void enterDfu();

  void updateUiSensors();
  void renderUi();
  bool startBluetooth();
  void restartBluetooth();
  // Stores whether Bluetooth should be switched on again at the next boot.
  void rememberBluetooth(bool on);
  void restoreBluetooth();
  uint8_t helloFlags() const;
  void loadStoredSettings();
  void persistSettings();
  void forgetPhone();
  void printStorageStatus();
  void rebootForUpdate();
  // Display wait hook: feeds the watchdog and, about every 20 ms, keeps the
  // BLE link and the actuator timing alive during an e-paper refresh.
  static void displayWaitHook();
  static void backgroundHook();
  void backgroundService();
  static void otaDataThunk(const uint8_t* data, size_t length);
  void applyUiAction(WatchUiAction action);
  void applyUiSelection();
  void processPhoneUpdates();
  bool applyPhoneUpdate(const PhoneUpdate& update);
  void storeNotification(uint16_t id, const char* app, const char* title,
                         const char* text);
  bool removeNotification(uint16_t id);
  void dismissShownNotification();
  void readBattery();
  void updateMusicPosition();
  void sendStatus();
  void sendSettings();
  void servicePhoneLink();
  void serviceHistory();
  void endCall();
  bool handleCallButton(bool select, bool back);

  void handleSerialByte(char value);
  void parseCapturedCommand();
  void handleCommand(char command);
  void pollButtons();
  void handleButton(size_t index);

  // Power policy inputs (see WatchPower and the README power section).
  void setDisplayLight(bool on);
  void serviceDisplayLight();
  void setDeepSleepMode(DeepSleepMode mode);
  // WatchSleepBusy bits: work that needs the full clock tree right now.
  uint8_t sleepBusy();
  // Milliseconds until the main loop next has something to do by itself.
  uint32_t sleepBudgetMs();
  static bool sleepVeto();

  TwoWire wire_;
  I2cBus i2c_;
  Bma400 accelerometer_;
  Bme280 environment_;
  Max17048 fuel_gauge_;
  SPIClass display_spi_;
  ET011TJ1 display_;
  WatchUi ui_;
  WatchUiModel model_;
  WatchRtc rtc_;
  WatchWatchdog watchdog_;
  WatchDfu dfu_;
  WatchBle ble_;
  WatchFlash flash_;
  WatchConfigStore config_;
  WatchOta ota_;
  bool display_light_on_ = false;
  WatchButtons buttons_;
  WatchDisplayController display_controller_;
  WatchDiagnostics diagnostics_;
  WatchActivity activity_;
  WatchAlarm alarm_;
  WatchPower power_;
  WatchPowerLog power_log_;

  static WatchApplication* instance_;
  bool in_background_ = false;
  uint32_t last_background_ms_ = 0;
  uint8_t preview_tone_ = 0;

  bool serial_capture_ = false;
  char serial_line_[48]{};
  uint8_t serial_line_length_ = 0;

  // Phone link session state (protocol v2).
  bool phone_session_ = false;      // HELLO received on this connection
  uint32_t last_status_ms_ = 0;
  int16_t last_status_battery_ = -1;
  bool history_sending_ = false;
  uint8_t history_next_ = 0;
  uint32_t music_base_position_ = 0;
  uint32_t music_base_ms_ = 0;

  // A notification that arrived with an alert is shown over the current
  // screen this long, then the screen underneath is drawn again.
  static constexpr uint32_t NOTIFICATION_PEEK_MS = 5000;
  bool peek_pending_ = false;  // set by BLE processing, drawn by the loop
  uint32_t peek_since_ms_ = 0;
  void serviceNotificationPeek();
  bool endNotificationPeek();

  // The phone stops ringing by itself after this long (FindPhoneRinger), so
  // the watch stops showing it as ringing then too.
  static constexpr uint32_t FIND_PHONE_RING_MS = 2UL * 60UL * 1000UL;
  uint32_t find_phone_since_ms_ = 0;
  void serviceFindPhone();

  // Front light switches itself off this long after it was turned on or a
  // button was last pressed.
  static constexpr uint32_t DISPLAY_LIGHT_TIMEOUT_MS = 30000;
  // The BME280 is only measured this often unless a screen that shows its
  // values is being drawn.
  static constexpr uint32_t ENVIRONMENT_INTERVAL_MS = 5UL * 60UL * 1000UL;
  static constexpr uint32_t STATUS_INTERVAL_MS = 10UL * 60UL * 1000UL;
  uint32_t display_light_since_ms_ = 0;
  // Double press of home on the clock face toggles the LED ring.
  static constexpr uint32_t DOUBLE_PRESS_MS = 600;
  bool home_press_armed_ = false;
  uint32_t home_press_ms_ = 0;
  // Any app goes back to the clock after this long without a button press.
  static constexpr uint32_t SCREEN_TIMEOUT_MS = 10000;
  uint32_t last_input_ms_ = 0;
  bool screenTimeoutHeld() const;
  void serviceScreenTimeout();
  // Double press of select on the clock plays/pauses music; a single press
  // opens the app menu after the window (only while music is loaded).
  bool select_press_armed_ = false;
  // Double press of the top-right button on the clock opens Messages at the
  // newest notification; a single press opens the app menu after the window.
  bool previous_press_armed_ = false;
  uint32_t previous_press_ms_ = 0;
  void servicePreviousPress();
  // USB is started only once the charger has finished probing a new port
  // (as at boot): stopped when the cable is pulled, restarted this long
  // after it is plugged in.
  static constexpr uint32_t USB_SETTLE_MS = 750;
  bool usb_started_ = false;       // Serial (USB CDC) attached
  bool usb_power_seen_ = false;    // USB power present at the last look
  uint32_t usb_power_since_ms_ = 0;
  void serviceUsb();
  uint32_t select_press_ms_ = 0;
  void serviceSelectPress();
  uint32_t environment_read_ms_ = 0;

  // Battery-use log (console c = dump, C twice = clear): one record a
  // minute in external flash, with deltas since the previous record.
  static constexpr uint32_t POWER_LOG_INTERVAL_MS = 60000;
  void writePowerLog(uint8_t type);
  void servicePowerLog();
  uint32_t power_log_ms_ = 0;
  WatchPower::Totals power_log_totals_{};
  uint32_t power_log_ble_rx_ = 0;
  uint32_t power_log_ble_tx_ = 0;
  uint32_t renders_ = 0;          // display refreshes since boot
  uint32_t render_ms_total_ = 0;  // time spent in them
  uint32_t power_log_renders_ = 0;
  uint32_t power_log_render_ms_ = 0;
  uint32_t power_log_clear_armed_ms_ = 0;
};
