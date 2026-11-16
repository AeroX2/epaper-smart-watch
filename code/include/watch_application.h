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
  void handleButton(size_t index, bool long_press);
  void handleChord(bool backslash, bool long_press);

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
};
