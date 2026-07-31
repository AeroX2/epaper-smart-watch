#pragma once

#include <Arduino.h>

enum class WatchScreen : uint8_t {
  Clock,
  Notifications,
  Timer,
  Alarms,
  Music,
  Weather,
  Sensors,
  Activity,
  Settings,
  Count,
};

enum class WatchUiAction : uint8_t {
  None,
  TimerChanged,
  AlarmChanged,
  NotificationDismissed,
  MediaPrevious,
  MediaPlayPause,
  MediaNext,
  BluetoothToggle,
  SettingsChanged,
};

struct WatchUiModel {
  uint8_t hour = 12;
  uint8_t minute = 0;
  uint8_t second = 0;
  uint8_t day = 1;
  uint8_t month = 1;
  uint16_t year = 2026;
  uint8_t weekday = 0;
  int16_t temperature_tenths_c = 0;
  uint8_t humidity_percent = 0;
  uint16_t pressure_hpa = 0;
  uint8_t battery_percent = 0;
  uint16_t battery_millivolts = 0;
  int16_t accel_x_mg = 0;
  int16_t accel_y_mg = 0;
  int16_t accel_z_mg = 0;
  uint32_t steps = 0;
  bool environment_valid = false;
  bool battery_valid = false;
  bool accelerometer_valid = false;
  bool time_valid = false;
  bool bluetooth_connected = false;
  bool led_ring_on = false;
  bool timer_running = false;
  uint32_t timer_remaining_seconds = 300;
  bool weekday_alarm_on = true;
  bool weekend_alarm_on = false;
  uint8_t alarm_hour = 7;
  uint8_t alarm_minute = 30;
  bool alarm_ringing = false;
  bool quiet_mode = false;
  bool vibration_on = true;
  bool notification_present = false;
  char notification_sender[25] = "ROBIN MEYER";
  char notification_line_1[29] = "LUNCH AT THE USUAL PLACE?";
  char notification_line_2[29] = "HEADING OUT AROUND 12:30.";
  bool phone_weather_valid = false;
  int16_t forecast_temperature_c = 0;
  int8_t forecast_high_c = 0;
  int8_t forecast_low_c = 0;
  char weather_condition[21] = "PARTLY CLOUDY";
  bool music_playing = false;
  uint16_t music_position_seconds = 108;
  uint16_t music_duration_seconds = 182;
  char music_title[25] = "PAPER MOON";
  char music_artist[25] = "LOW TIDE RADIO";
};

class MonochromeCanvas {
 public:
  static constexpr int16_t WIDTH = 240;
  static constexpr int16_t HEIGHT = 240;
  static constexpr size_t STRIDE = WIDTH / 8;
  static constexpr size_t BYTES = STRIDE * HEIGHT;

  void clear(bool black = false);
  void pixel(int16_t x, int16_t y, bool black = true);
  void line(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
            bool black = true);
  void rect(int16_t x, int16_t y, int16_t width, int16_t height,
            bool black = true);
  void fillRect(int16_t x, int16_t y, int16_t width, int16_t height,
                bool black = true);
  void circle(int16_t center_x, int16_t center_y, int16_t radius,
              bool black = true);
  void triangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2,
                int16_t y2, bool black = true);

  int16_t textWidth(const char* value, uint8_t scale = 1) const;
  void text(const char* value, int16_t x, int16_t y, uint8_t scale = 1,
            bool black = true);
  void centeredText(const char* value, int16_t center_x, int16_t y,
                    uint8_t scale = 1, bool black = true);

  const uint8_t* data() const;
  uint8_t* data();

 private:
  static uint8_t glyphColumn(char character, uint8_t column);
  static bool insidePanel(int16_t x, int16_t y);
  void character(char value, int16_t x, int16_t y, uint8_t scale,
                 bool black);

  uint8_t pixels_[BYTES]{};
};

class WatchUi {
 public:
  WatchUi();

  WatchUiAction previous(WatchUiModel& model);
  WatchUiAction next(WatchUiModel& model);
  void home();
  void showScreen(WatchScreen screen);
  WatchUiAction select(WatchUiModel& model);
  void render(const WatchUiModel& model);

  WatchScreen screen() const;
  bool isInside() const;
  const char* screenName() const;
  const uint8_t* framebuffer() const;
  static constexpr size_t framebufferStride() {
    return MonochromeCanvas::STRIDE;
  }

 private:
  void drawClock(const WatchUiModel& model);
  void drawNotifications(const WatchUiModel& model);
  void drawTimer(const WatchUiModel& model);
  void drawAlarms(const WatchUiModel& model);
  void drawMusic(const WatchUiModel& model);
  void drawWeather(const WatchUiModel& model);
  void drawSensors(const WatchUiModel& model);
  void drawActivity(const WatchUiModel& model);
  void drawSettings(const WatchUiModel& model);
  void drawCardFrame(const char* title, const char* footer);
  void drawPageDots();
  void drawProgress(int16_t x, int16_t y, int16_t width, uint8_t percent,
                    int16_t height = 8);
  void drawToggle(int16_t x, int16_t y, bool enabled);
  void drawBattery(int16_t x, int16_t y, uint8_t percent);

  MonochromeCanvas canvas_;
  WatchScreen screen_ = WatchScreen::Clock;
  bool inside_ = false;
  uint8_t selected_setting_ = 0;
  uint8_t selected_alarm_ = 0;
};
