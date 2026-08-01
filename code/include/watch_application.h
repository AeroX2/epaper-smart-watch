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
#include "watch_diagnostics.h"
#include "watch_display.h"
#include "watch_power.h"
#include "watch_rtc.h"
#include "watch_dfu.h"
#include "watch_ui.h"

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
  void applyUiAction(WatchUiAction action);
  void applyUiSelection();
  void processPhoneUpdates();

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
  WatchDfu dfu_;
  WatchBle ble_;
  bool display_light_on_ = false;
  WatchButtons buttons_;
  WatchDisplayController display_controller_;
  WatchDiagnostics diagnostics_;
  WatchActivity activity_;
  WatchAlarm alarm_;
  WatchPower power_;

  bool serial_capture_ = false;
  char serial_line_[48]{};
  uint8_t serial_line_length_ = 0;
};
