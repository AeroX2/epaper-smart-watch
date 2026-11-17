#pragma once

#include <Arduino.h>

#include "watch_assets.h"

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
  SettingsChanged,       // local-only setting (LED ring)
  PhoneSettingsChanged,  // vibration or quiet mode: synced to the phone
  FindPhoneToggle,
  AlertToneChanged,      // model.alert_tone cycled: sync, persist, preview
  SecureLinkChanged,     // model.secure_link toggled
  ForgetPhone,           // confirmed on the Settings row
};

// Full-screen firmware-update overlay (drawn instead of the current screen).
enum class OtaScreen : uint8_t {
  None,
  Confirm,     // ota_version / ota_size: select = install, back = decline
  Progress,    // ota_percent
  Verifying,
  Installing,  // shown through the reboot while the bootloader installs
  Failed,      // ota_detail; any button dismisses
};

struct WatchNotification {
  uint16_t id = 0;  // 1-65535 from the phone; 0 = local (Sleep as Android)
  char app[25]{};
  char title[29]{};
  char text[57]{};
};

enum class MusicState : uint8_t { None = 0, Playing = 1, Paused = 2 };

struct WatchUiModel {
  static constexpr uint8_t MAX_NOTIFICATIONS = 5;

  uint8_t hour = 12;
  uint8_t minute = 0;
  uint8_t second = 0;
  uint8_t day = 1;
  uint8_t month = 1;
  uint16_t year = 2026;
  uint8_t weekday = 0;
  // RTC time is local; utc = local_epoch - utc_offset_seconds.
  uint32_t local_epoch = 0;
  int32_t utc_offset_seconds = 0;
  int16_t temperature_tenths_c = 0;
  uint8_t humidity_percent = 0;
  uint16_t pressure_hpa = 0;
  uint32_t pressure_pa = 0;  // the same reading, unrounded
  // Sea-level pressure from the phone's weather, tenths of a hPa; 0 = unknown.
  uint16_t sea_level_pressure_tenths_hpa = 0;
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
  bool bluetooth_enabled = false;
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
  // Notification alert tone: 0 none, 1 Chirp, 2 Double, 3 Rise, 4 Fall,
  // 5 Triple.
  uint8_t alert_tone = 0;
  bool secure_link = false;            // persisted "SECURE LINK" setting
  bool secure_link_available = false;  // needs working external flash
  bool phone_bonded = false;           // a bond key is stored

  OtaScreen ota_screen = OtaScreen::None;
  uint8_t ota_percent = 0;
  uint32_t ota_size = 0;
  char ota_version[17]{};
  char ota_detail[9]{};

  // Newest first.
  WatchNotification notifications[MAX_NOTIFICATIONS]{};
  uint8_t notification_count = 0;

  bool call_active = false;
  char call_caller[29]{};

  bool phone_weather_valid = false;
  int16_t forecast_temperature_c = 0;
  int8_t forecast_high_c = 0;
  int8_t forecast_low_c = 0;
  int16_t weather_code = -1;
  char weather_condition[21] = "PARTLY CLOUDY";

  MusicState music_state = MusicState::None;
  uint32_t music_position_seconds = 0;  // extrapolated while playing
  uint32_t music_duration_seconds = 0;
  char music_title[25]{};
  char music_artist[25]{};

  // Phone-provided UTC epochs, 0 = none.
  uint32_t next_alarm_utc = 0;
  uint32_t event_utc = 0;
  char event_title[29]{};

  bool phone_status_valid = false;
  uint8_t phone_battery_percent = 0;
  bool phone_charging = false;
  bool phone_dnd = false;
  bool find_phone_active = false;
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
  void fillCircle(int16_t center_x, int16_t center_y, int16_t radius,
                  bool black = true);

  // How much of a filled area is black. The dither patterns are fixed to
  // the panel grid, so neighbouring areas line up. Shaded fills are opaque:
  // they write the white pixels of the pattern as well as the black ones.
  enum class Shade : uint8_t {
    White,
    Dither12,
    Dither25,
    Dither50,
    Dither75,
    Black,
    Rows2,         // a black row every 2 rows
    Rows3,         // a black row every 3 rows
    Rows3Inverse,  // a white row every 3 rows
  };
  struct PolygonPoint {
    float x;
    float y;
  };
  static constexpr uint8_t MAX_POLYGON_POINTS = 12;
  void shadeRect(int16_t x, int16_t y, int16_t width, int16_t height,
                 Shade shade);
  // Even-odd scanline fill; the polygon is closed automatically.
  void fillPolygon(const PolygonPoint* points, uint8_t count, Shade shade);

  int16_t textWidth(const char* value, uint8_t scale = 1) const;
  void text(const char* value, int16_t x, int16_t y, uint8_t scale = 1,
            bool black = true);
  void centeredText(const char* value, int16_t center_x, int16_t y,
                    uint8_t scale = 1, bool black = true);
  int16_t fontTextWidth(const BitmapFont& font, const char* value) const;
  void fontText(const BitmapFont& font, const char* value, int16_t x,
                int16_t y, bool black = true);
  void centeredFontText(const BitmapFont& font, const char* value,
                        int16_t center_x, int16_t y, bool black = true);
  void centeredFittedFontText(const BitmapFont& font, const char* value,
                              int16_t center_x, int16_t y,
                              int16_t max_width, bool black = true);
  void icon(WatchIcon value, int16_t center_x, int16_t center_y,
            uint8_t size = 16, bool black = true);

  const uint8_t* data() const;

 private:
  static uint8_t glyphColumn(char character, uint8_t column);
  static bool insidePanel(int16_t x, int16_t y);
  static bool shadeAt(Shade shade, int16_t x, int16_t y);
  void backgroundPixel(int16_t x, int16_t y, bool black);
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
  // Index (newest first) of the notification shown on the Messages card.
  uint8_t notificationIndex() const;
  void clampNotificationIndex(uint8_t count);
  void setNotificationIndex(uint8_t index);
  const uint8_t* framebuffer() const;
  static constexpr size_t framebufferStride() {
    return MonochromeCanvas::STRIDE;
  }

 private:
  void drawClock(const WatchUiModel& model);
  void drawClassicClock(const WatchUiModel& model);
  void drawLakeClock(const WatchUiModel& model);
  void drawLauncher(const WatchUiModel& model);
  void drawNotifications(const WatchUiModel& model);
  void drawTimer(const WatchUiModel& model);
  void drawAlarms(const WatchUiModel& model);
  void drawMusic(const WatchUiModel& model);
  void drawWeather(const WatchUiModel& model);
  void drawSensors(const WatchUiModel& model);
  void drawActivity(const WatchUiModel& model);
  void drawSettings(const WatchUiModel& model);
  void drawCall(const WatchUiModel& model);
  void drawOta(const WatchUiModel& model);
  void drawUpcoming(const WatchUiModel& model, int16_t y);
  void drawWrappedText(const BitmapFont& font, const char* text,
                       int16_t center_x, int16_t y, int16_t max_width,
                       uint8_t max_lines, int16_t line_height);
  void drawCardFrame(const char* title, const char* footer);
  void drawProgress(int16_t x, int16_t y, int16_t width, uint8_t percent,
                    int16_t height = 8);
  void drawToggle(int16_t x, int16_t y, bool enabled);

  MonochromeCanvas canvas_;
  WatchScreen screen_ = WatchScreen::Clock;
  bool inside_ = false;
  uint8_t selected_setting_ = 0;
  uint8_t settings_top_ = 0;   // first visible row of the scrolling list
  bool forget_armed_ = false;  // FORGET PHONE pressed once, awaiting confirm
  uint8_t selected_alarm_ = 0;
  uint8_t notification_index_ = 0;
};
