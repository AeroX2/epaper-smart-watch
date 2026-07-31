#pragma once

#include <Arduino.h>

enum class PhoneUpdateType : uint8_t {
  None,
  Time,
  Notification,
  ClearNotification,
  Weather,
  Music,
  ResetSteps,
  SleepStart,
  SleepStop,
  SleepPause,
  SleepAlarmStart,
  SleepAlarmStop,
  SleepNotification,
  SleepHint,
};

struct PhoneUpdate {
  PhoneUpdateType type = PhoneUpdateType::None;
  time_t epoch = 0;
  char sender[25]{};
  char line_1[29]{};
  char line_2[29]{};
  int16_t temperature_c = 0;
  int8_t high_c = 0;
  int8_t low_c = 0;
  char condition[21]{};
  bool playing = false;
  uint16_t position_seconds = 0;
  uint16_t duration_seconds = 0;
  char title[25]{};
  char artist[25]{};
  uint32_t value = 0;
};

class WatchBle {
 public:
  bool begin(Print& out);
  void end(Print& out);
  void poll();
  bool takeUpdate(PhoneUpdate& update);
  bool sendCommand(const char* command);
  void updateBattery(uint8_t percent);

  bool ready() const;
  bool connected() const;

 private:
  void parseMessage(char* message);

  bool ready_ = false;
  bool connected_ = false;
  bool update_pending_ = false;
  uint8_t last_battery_ = 0xFF;
  PhoneUpdate pending_{};
};
