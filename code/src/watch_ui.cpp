#include "watch_ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {

constexpr const char* WEEKDAYS[] = {
    "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT",
};

constexpr const char* MONTHS[] = {
    "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
    "JUL", "AUG", "SEP", "OCT", "NOV", "DEC",
};

uint8_t clampPercent(int32_t value) {
  if (value < 0) {
    return 0;
  }
  if (value > 100) {
    return 100;
  }
  return static_cast<uint8_t>(value);
}

}  // namespace

void MonochromeCanvas::clear(bool black) {
  memset(pixels_, black ? 0xFF : 0x00, sizeof(pixels_));
}

bool MonochromeCanvas::insidePanel(int16_t x, int16_t y) {
  if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) {
    return false;
  }
  const int32_t dx = static_cast<int32_t>(x) * 2 - (WIDTH - 1);
  const int32_t dy = static_cast<int32_t>(y) * 2 - (HEIGHT - 1);
  constexpr int32_t diameter = 236;
  return dx * dx + dy * dy <= diameter * diameter;
}

void MonochromeCanvas::pixel(int16_t x, int16_t y, bool black) {
  if (!insidePanel(x, y)) {
    return;
  }
  uint8_t& packed = pixels_[static_cast<size_t>(y) * STRIDE + x / 8];
  const uint8_t mask = static_cast<uint8_t>(0x80U >> (x & 7));
  if (black) {
    packed |= mask;
  } else {
    packed &= static_cast<uint8_t>(~mask);
  }
}

void MonochromeCanvas::line(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                            bool black) {
  const int16_t dx = abs(x1 - x0);
  const int16_t sx = x0 < x1 ? 1 : -1;
  const int16_t dy = -abs(y1 - y0);
  const int16_t sy = y0 < y1 ? 1 : -1;
  int16_t error = dx + dy;
  while (true) {
    pixel(x0, y0, black);
    if (x0 == x1 && y0 == y1) {
      break;
    }
    const int16_t twice_error = static_cast<int16_t>(2 * error);
    if (twice_error >= dy) {
      error += dy;
      x0 += sx;
    }
    if (twice_error <= dx) {
      error += dx;
      y0 += sy;
    }
  }
}

void MonochromeCanvas::rect(int16_t x, int16_t y, int16_t width,
                            int16_t height, bool black) {
  if (width <= 0 || height <= 0) {
    return;
  }
  line(x, y, x + width - 1, y, black);
  line(x, y + height - 1, x + width - 1, y + height - 1, black);
  line(x, y, x, y + height - 1, black);
  line(x + width - 1, y, x + width - 1, y + height - 1, black);
}

void MonochromeCanvas::fillRect(int16_t x, int16_t y, int16_t width,
                                int16_t height, bool black) {
  if (width <= 0 || height <= 0) {
    return;
  }
  for (int16_t row = 0; row < height; ++row) {
    line(x, y + row, x + width - 1, y + row, black);
  }
}

void MonochromeCanvas::circle(int16_t center_x, int16_t center_y,
                              int16_t radius, bool black) {
  int16_t x = radius;
  int16_t y = 0;
  int16_t error = 1 - radius;
  while (x >= y) {
    pixel(center_x + x, center_y + y, black);
    pixel(center_x + y, center_y + x, black);
    pixel(center_x - y, center_y + x, black);
    pixel(center_x - x, center_y + y, black);
    pixel(center_x - x, center_y - y, black);
    pixel(center_x - y, center_y - x, black);
    pixel(center_x + y, center_y - x, black);
    pixel(center_x + x, center_y - y, black);
    ++y;
    if (error < 0) {
      error += static_cast<int16_t>(2 * y + 1);
    } else {
      --x;
      error += static_cast<int16_t>(2 * (y - x) + 1);
    }
  }
}

void MonochromeCanvas::triangle(int16_t x0, int16_t y0, int16_t x1,
                                int16_t y1, int16_t x2, int16_t y2,
                                bool black) {
  line(x0, y0, x1, y1, black);
  line(x1, y1, x2, y2, black);
  line(x2, y2, x0, y0, black);
}

int16_t MonochromeCanvas::textWidth(const char* value, uint8_t scale) const {
  if (value == nullptr || *value == '\0') {
    return 0;
  }
  return static_cast<int16_t>((strlen(value) * 6 - 1) * scale);
}

void MonochromeCanvas::text(const char* value, int16_t x, int16_t y,
                            uint8_t scale, bool black) {
  if (value == nullptr || scale == 0) {
    return;
  }
  while (*value != '\0') {
    character(*value++, x, y, scale, black);
    x += static_cast<int16_t>(6 * scale);
  }
}

void MonochromeCanvas::centeredText(const char* value, int16_t center_x,
                                    int16_t y, uint8_t scale, bool black) {
  text(value, center_x - textWidth(value, scale) / 2, y, scale, black);
}

void MonochromeCanvas::character(char value, int16_t x, int16_t y,
                                 uint8_t scale, bool black) {
  if (value >= 'a' && value <= 'z') {
    value = static_cast<char>(value - 'a' + 'A');
  }
  for (uint8_t column = 0; column < 5; ++column) {
    const uint8_t bits = glyphColumn(value, column);
    for (uint8_t row = 0; row < 7; ++row) {
      if ((bits & (1U << row)) != 0) {
        fillRect(x + column * scale, y + row * scale, scale, scale, black);
      }
    }
  }
}

const uint8_t* MonochromeCanvas::data() const {
  return pixels_;
}

uint8_t* MonochromeCanvas::data() {
  return pixels_;
}

uint8_t MonochromeCanvas::glyphColumn(char character, uint8_t column) {
  const uint8_t* glyph = nullptr;
  static constexpr uint8_t DIGITS[][5] = {
      {0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00},
      {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4B, 0x31},
      {0x18, 0x14, 0x12, 0x7F, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
      {0x3C, 0x4A, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},
      {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1E},
  };
  static constexpr uint8_t LETTERS[][5] = {
      {0x7E, 0x11, 0x11, 0x11, 0x7E}, {0x7F, 0x49, 0x49, 0x49, 0x36},
      {0x3E, 0x41, 0x41, 0x41, 0x22}, {0x7F, 0x41, 0x41, 0x22, 0x1C},
      {0x7F, 0x49, 0x49, 0x49, 0x41}, {0x7F, 0x09, 0x09, 0x09, 0x01},
      {0x3E, 0x41, 0x49, 0x49, 0x7A}, {0x7F, 0x08, 0x08, 0x08, 0x7F},
      {0x00, 0x41, 0x7F, 0x41, 0x00}, {0x20, 0x40, 0x41, 0x3F, 0x01},
      {0x7F, 0x08, 0x14, 0x22, 0x41}, {0x7F, 0x40, 0x40, 0x40, 0x40},
      {0x7F, 0x02, 0x0C, 0x02, 0x7F}, {0x7F, 0x04, 0x08, 0x10, 0x7F},
      {0x3E, 0x41, 0x41, 0x41, 0x3E}, {0x7F, 0x09, 0x09, 0x09, 0x06},
      {0x3E, 0x41, 0x51, 0x21, 0x5E}, {0x7F, 0x09, 0x19, 0x29, 0x46},
      {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7F, 0x01, 0x01},
      {0x3F, 0x40, 0x40, 0x40, 0x3F}, {0x1F, 0x20, 0x40, 0x20, 0x1F},
      {0x3F, 0x40, 0x38, 0x40, 0x3F}, {0x63, 0x14, 0x08, 0x14, 0x63},
      {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43},
  };
  static constexpr uint8_t SPACE[5] = {};
  static constexpr uint8_t COLON[5] = {0x00, 0x36, 0x36, 0x00, 0x00};
  static constexpr uint8_t PERIOD[5] = {0x00, 0x60, 0x60, 0x00, 0x00};
  static constexpr uint8_t PERCENT[5] = {0x63, 0x13, 0x08, 0x64, 0x63};
  static constexpr uint8_t DASH[5] = {0x08, 0x08, 0x08, 0x08, 0x08};
  static constexpr uint8_t SLASH[5] = {0x20, 0x10, 0x08, 0x04, 0x02};
  static constexpr uint8_t PLUS[5] = {0x08, 0x08, 0x3E, 0x08, 0x08};
  static constexpr uint8_t QUESTION[5] = {0x02, 0x01, 0x51, 0x09, 0x06};
  static constexpr uint8_t EXCLAMATION[5] = {0x00, 0x00, 0x5F, 0x00, 0x00};

  if (character >= '0' && character <= '9') {
    glyph = DIGITS[character - '0'];
  } else if (character >= 'A' && character <= 'Z') {
    glyph = LETTERS[character - 'A'];
  } else {
    switch (character) {
      case ':':
        glyph = COLON;
        break;
      case '.':
        glyph = PERIOD;
        break;
      case '%':
        glyph = PERCENT;
        break;
      case '-':
        glyph = DASH;
        break;
      case '/':
        glyph = SLASH;
        break;
      case '+':
        glyph = PLUS;
        break;
      case '!':
        glyph = EXCLAMATION;
        break;
      case '?':
        glyph = QUESTION;
        break;
      default:
        glyph = SPACE;
        break;
    }
  }
  return glyph[column < 5 ? column : 0];
}

WatchUi::WatchUi() = default;

WatchUiAction WatchUi::previous(WatchUiModel& model) {
  if (!inside_) {
    const uint8_t first = static_cast<uint8_t>(WatchScreen::Notifications);
    const uint8_t last = static_cast<uint8_t>(WatchScreen::Count) - 1;
    const uint8_t current = static_cast<uint8_t>(screen_);
    screen_ = static_cast<WatchScreen>(
        current <= first ? last : current - 1);
    return WatchUiAction::None;
  }
  if (screen_ == WatchScreen::Timer && !model.timer_running) {
    const uint32_t minutes = model.timer_remaining_seconds / 60;
    model.timer_remaining_seconds = (minutes > 1 ? minutes - 1 : 60) * 60;
    return WatchUiAction::TimerChanged;
  }
  if (screen_ == WatchScreen::Music) {
    return WatchUiAction::MediaPrevious;
  }
  if (screen_ == WatchScreen::Settings) {
    selected_setting_ =
        selected_setting_ == 0 ? 3 : selected_setting_ - 1;
    return WatchUiAction::None;
  }
  if (screen_ == WatchScreen::Alarms) {
    selected_alarm_ = selected_alarm_ == 0 ? 1 : 0;
  }
  return WatchUiAction::None;
}

WatchUiAction WatchUi::next(WatchUiModel& model) {
  if (!inside_) {
    const uint8_t first = static_cast<uint8_t>(WatchScreen::Notifications);
    const uint8_t last = static_cast<uint8_t>(WatchScreen::Count) - 1;
    const uint8_t current = static_cast<uint8_t>(screen_);
    screen_ = static_cast<WatchScreen>(
        current < first || current >= last ? first : current + 1);
    return WatchUiAction::None;
  }
  if (screen_ == WatchScreen::Timer && !model.timer_running) {
    const uint32_t minutes = model.timer_remaining_seconds / 60;
    model.timer_remaining_seconds = (minutes < 60 ? minutes + 1 : 1) * 60;
    return WatchUiAction::TimerChanged;
  }
  if (screen_ == WatchScreen::Music) {
    return WatchUiAction::MediaNext;
  }
  if (screen_ == WatchScreen::Settings) {
    selected_setting_ = static_cast<uint8_t>((selected_setting_ + 1) % 4);
    return WatchUiAction::None;
  }
  if (screen_ == WatchScreen::Alarms) {
    selected_alarm_ = static_cast<uint8_t>((selected_alarm_ + 1) % 2);
  }
  return WatchUiAction::None;
}

void WatchUi::home() {
  if (inside_) {
    inside_ = false;
  } else {
    screen_ = WatchScreen::Clock;
  }
}

void WatchUi::showScreen(WatchScreen screen) {
  if (screen < WatchScreen::Count) {
    screen_ = screen;
    inside_ = false;
  }
}

WatchUiAction WatchUi::select(WatchUiModel& model) {
  if (screen_ == WatchScreen::Clock) {
    screen_ = WatchScreen::Notifications;
    inside_ = false;
    return WatchUiAction::None;
  }
  if (!inside_) {
    inside_ = true;
    return WatchUiAction::None;
  }
  switch (screen_) {
    case WatchScreen::Notifications:
      model.notification_present = false;
      return WatchUiAction::NotificationDismissed;
    case WatchScreen::Timer:
      if (!model.timer_running && model.timer_remaining_seconds == 0) {
        model.timer_remaining_seconds = 5 * 60;
      }
      model.timer_running = !model.timer_running;
      return WatchUiAction::TimerChanged;
    case WatchScreen::Alarms:
      if (selected_alarm_ == 0) {
        model.weekday_alarm_on = !model.weekday_alarm_on;
      } else {
        model.weekend_alarm_on = !model.weekend_alarm_on;
      }
      return WatchUiAction::AlarmChanged;
    case WatchScreen::Music:
      model.music_playing = !model.music_playing;
      return WatchUiAction::MediaPlayPause;
    case WatchScreen::Settings:
      if (selected_setting_ == 0) {
        return WatchUiAction::BluetoothToggle;
      } else if (selected_setting_ == 1) {
        model.quiet_mode = !model.quiet_mode;
      } else if (selected_setting_ == 2) {
        model.led_ring_on = !model.led_ring_on;
      } else {
        model.vibration_on = !model.vibration_on;
      }
      return WatchUiAction::SettingsChanged;
    default:
      return WatchUiAction::None;
  }
  return WatchUiAction::None;
}

void WatchUi::render(const WatchUiModel& model) {
  canvas_.clear();
  switch (screen_) {
    case WatchScreen::Clock:
      drawClock(model);
      break;
    case WatchScreen::Notifications:
      drawNotifications(model);
      break;
    case WatchScreen::Timer:
      drawTimer(model);
      break;
    case WatchScreen::Alarms:
      drawAlarms(model);
      break;
    case WatchScreen::Music:
      drawMusic(model);
      break;
    case WatchScreen::Weather:
      drawWeather(model);
      break;
    case WatchScreen::Sensors:
      drawSensors(model);
      break;
    case WatchScreen::Activity:
      drawActivity(model);
      break;
    case WatchScreen::Settings:
      drawSettings(model);
      break;
    default:
      drawClock(model);
      break;
  }
}

WatchScreen WatchUi::screen() const {
  return screen_;
}

bool WatchUi::isInside() const {
  return inside_;
}

const char* WatchUi::screenName() const {
  static constexpr const char* NAMES[] = {
      "clock", "notifications", "timer",    "alarms",  "music",
      "weather", "sensors",       "activity", "settings",
  };
  return NAMES[static_cast<uint8_t>(screen_)];
}

const uint8_t* WatchUi::framebuffer() const {
  return canvas_.data();
}

void WatchUi::drawClock(const WatchUiModel& model) {
  char buffer[32];
  const char* weekday = WEEKDAYS[model.weekday % 7];
  const char* month = MONTHS[(model.month >= 1 && model.month <= 12)
                                 ? model.month - 1
                                 : 0];
  // Claude reference face: asymmetric stacked time with a narrow data rail.
  snprintf(buffer, sizeof(buffer), "%02u", model.hour);
  canvas_.text(buffer, 50, 46, 9);
  snprintf(buffer, sizeof(buffer), "%02u", model.minute);
  canvas_.text(buffer, 54, 127, 9);
  canvas_.fillRect(160, 62, 2, 118);

  canvas_.text(weekday, 174, 62, 1);
  snprintf(buffer, sizeof(buffer), "%u %s", model.day, month);
  canvas_.text(buffer, 174, 75, 1);

  if (model.battery_valid) {
    drawBattery(174, 101, model.battery_percent);
    snprintf(buffer, sizeof(buffer), "%u%%", model.battery_percent);
  } else {
    drawBattery(174, 101, 0);
    snprintf(buffer, sizeof(buffer), "--%%");
  }
  canvas_.text(buffer, 174, 116, 1);

  if (model.environment_valid) {
    snprintf(buffer, sizeof(buffer), "%dC", model.temperature_tenths_c / 10);
    canvas_.text(buffer, 174, 137, 2);
    snprintf(buffer, sizeof(buffer), "%u%% RH", model.humidity_percent);
  } else {
    canvas_.text("--C", 174, 137, 2);
    snprintf(buffer, sizeof(buffer), "--%% RH");
  }
  canvas_.text(buffer, 174, 156, 1);
  canvas_.line(173, 168, 218, 168);

  canvas_.text("ALARM", 174, 175, 1);
  snprintf(buffer, sizeof(buffer), "%02u:%02u", model.alarm_hour,
           model.alarm_minute);
  canvas_.text(buffer, 174, 186, 1);
}

void WatchUi::drawCardFrame(const char* title, const char* footer) {
  canvas_.centeredText(title, 120, 16, 1);
  if (inside_) {
    canvas_.text("IN", 40, 16, 1);
    canvas_.centeredText(footer, 120, 218, 1);
  } else {
    canvas_.triangle(10, 118, 17, 113, 17, 123);
    canvas_.triangle(230, 118, 223, 113, 223, 123);
    drawPageDots();
    canvas_.centeredText("SELECT OPEN", 120, 218, 1);
  }
}

void WatchUi::drawPageDots() {
  constexpr uint8_t count = static_cast<uint8_t>(WatchScreen::Count) - 1;
  constexpr int16_t start = 85;
  for (uint8_t index = 0; index < count; ++index) {
    const int16_t x = start + index * 10;
    if (index + 1 == static_cast<uint8_t>(screen_)) {
      canvas_.fillRect(x, 202, 5, 5);
    } else {
      canvas_.rect(x, 202, 5, 5);
    }
  }
}

void WatchUi::drawProgress(int16_t x, int16_t y, int16_t width,
                           uint8_t percent, int16_t height) {
  canvas_.rect(x, y, width, height);
  const int16_t fill =
      static_cast<int16_t>((width - 4) * clampPercent(percent) / 100);
  canvas_.fillRect(x + 2, y + 2, fill, height - 4);
}

void WatchUi::drawToggle(int16_t x, int16_t y, bool enabled) {
  canvas_.rect(x, y, 28, 13);
  canvas_.fillRect(enabled ? x + 17 : x + 3, y + 3, 8, 7);
}

void WatchUi::drawBattery(int16_t x, int16_t y, uint8_t percent) {
  canvas_.rect(x, y, 29, 12);
  canvas_.fillRect(x + 29, y + 4, 3, 4);
  canvas_.fillRect(x + 2, y + 2,
                   static_cast<int16_t>(23 * clampPercent(percent) / 100), 8);
}

void WatchUi::drawNotifications(const WatchUiModel& model) {
  drawCardFrame("MESSAGE 2 MIN", "SELECT DISMISS");
  if (!model.notification_present) {
    canvas_.centeredText("NO NOTIFICATIONS", 120, 93, 2);
    return;
  }
  canvas_.text(model.notification_sender, 42, 53, 2);
  canvas_.text(model.notification_line_1, 42, 79, 1);
  canvas_.text(model.notification_line_2, 42, 101, 1);
  canvas_.line(42, 141, 198, 141);
  canvas_.text("SIGNAL", 42, 151, 1);
  canvas_.text("REPLY ON PHONE", 42, 165, 1);
}

void WatchUi::drawTimer(const WatchUiModel& model) {
  drawCardFrame("TIMER", "PREV/NEXT  SELECT GO");
  char buffer[12];
  const uint32_t minutes = model.timer_remaining_seconds / 60;
  const uint32_t seconds = model.timer_remaining_seconds % 60;
  snprintf(buffer, sizeof(buffer), "%02lu:%02lu",
           static_cast<unsigned long>(minutes),
           static_cast<unsigned long>(seconds));
  canvas_.centeredText(buffer, 120, 66, 5);
  canvas_.centeredText(model.timer_running ? "RUNNING" : "READY", 120, 112, 1);
  canvas_.fillRect(67, 133, 106, 2);
  canvas_.centeredText("VIBRATION AT FINISH", 120, 151, 1);
}

void WatchUi::drawAlarms(const WatchUiModel& model) {
  drawCardFrame("ALARMS", "SELECT TOGGLE");
  char alarm[8];
  snprintf(alarm, sizeof(alarm), "%02u:%02u", model.alarm_hour,
           model.alarm_minute);
  canvas_.text(alarm, 45, 55, 3);
  canvas_.text("MO TU WE TH FR", 46, 80, 1);
  drawToggle(163, 60, model.weekday_alarm_on);
  if (inside_ && selected_alarm_ == 0) {
    canvas_.rect(40, 50, 160, 45);
  }
  canvas_.line(42, 101, 198, 101);
  canvas_.text("09:00", 45, 114, 3);
  canvas_.text("SA SU", 46, 139, 1);
  drawToggle(163, 119, model.weekend_alarm_on);
  if (inside_ && selected_alarm_ == 1) {
    canvas_.rect(40, 109, 160, 45);
  }
  if (model.alarm_ringing) {
    canvas_.centeredText("RINGING", 120, 174, 2);
  }
}

void WatchUi::drawMusic(const WatchUiModel& model) {
  drawCardFrame("PLAYING", "PREV  PLAY  NEXT");
  canvas_.text(model.music_title, 42, 51, 2);
  canvas_.text(model.music_artist, 42, 72, 1);
  const uint8_t progress =
      model.music_duration_seconds == 0
          ? 0
          : static_cast<uint8_t>(
                min(100UL, static_cast<unsigned long>(
                               model.music_position_seconds) *
                               100UL / model.music_duration_seconds));
  drawProgress(42, 95, 156, progress);
  char time_value[10];
  snprintf(time_value, sizeof(time_value), "%u:%02u",
           model.music_position_seconds / 60,
           model.music_position_seconds % 60);
  canvas_.text(time_value, 42, 108, 1);
  snprintf(time_value, sizeof(time_value), "%u:%02u",
           model.music_duration_seconds / 60,
           model.music_duration_seconds % 60);
  canvas_.text(time_value, 175, 108, 1);
  canvas_.text("|<", 54, 139, 2);
  canvas_.circle(120, 146, 22);
  if (model.music_playing) {
    canvas_.fillRect(113, 135, 5, 22);
    canvas_.fillRect(123, 135, 5, 22);
  } else {
    for (int16_t row = 0; row < 23; ++row) {
      canvas_.line(113, 135 + row, 113 + row * 3 / 4, 135 + row);
    }
  }
  canvas_.text(">|", 166, 139, 2);
  canvas_.centeredText(model.music_playing ? "PLAYING 1/2" : "PAUSED 1/2", 120,
                       178, 1);
}

void WatchUi::drawWeather(const WatchUiModel& model) {
  drawCardFrame("SYDNEY", "PHONE DATA");
  char buffer[20];
  if (model.phone_weather_valid) {
    snprintf(buffer, sizeof(buffer), "%dC", model.forecast_temperature_c);
  } else if (model.environment_valid) {
    snprintf(buffer, sizeof(buffer), "%dC", model.temperature_tenths_c / 10);
  } else {
    snprintf(buffer, sizeof(buffer), "--C");
  }
  canvas_.centeredText(buffer, 120, 50, 5);
  canvas_.centeredText(model.weather_condition, 120, 94, 1);
  if (model.phone_weather_valid) {
    snprintf(buffer, sizeof(buffer), "H %dC  L %dC", model.forecast_high_c,
             model.forecast_low_c);
  } else {
    snprintf(buffer, sizeof(buffer), "PHONE FORECAST --");
  }
  canvas_.centeredText(buffer, 120, 112, 1);
  canvas_.text("12H", 58, 143, 1);
  canvas_.text("19C", 58, 157, 1);
  canvas_.text("15H", 108, 143, 1);
  canvas_.text("18C", 108, 157, 1);
  canvas_.text("18H", 158, 143, 1);
  canvas_.text("16C", 158, 157, 1);
  canvas_.centeredText("RAIN 10%", 120, 181, 1);
}

void WatchUi::drawSensors(const WatchUiModel& model) {
  drawCardFrame("SENSORS", "DEVICE DATA");
  char buffer[24];
  canvas_.text("ACCEL", 47, 50, 1);
  if (model.accelerometer_valid) {
    const int32_t magnitude =
        abs(model.accel_x_mg) + abs(model.accel_y_mg) + abs(model.accel_z_mg);
    snprintf(buffer, sizeof(buffer), "%ld MG", static_cast<long>(magnitude));
  } else {
    snprintf(buffer, sizeof(buffer), "--");
  }
  canvas_.text(buffer, 130, 50, 1);
  canvas_.line(47, 68, 193, 68);

  canvas_.text("PRESS", 47, 78, 1);
  snprintf(buffer, sizeof(buffer), model.environment_valid ? "%u HPA" : "--",
           model.pressure_hpa);
  canvas_.text(buffer, 130, 78, 1);
  canvas_.line(47, 96, 193, 96);

  canvas_.text("TEMP", 47, 106, 1);
  if (model.environment_valid) {
    snprintf(buffer, sizeof(buffer), "%d.%d C",
             model.temperature_tenths_c / 10,
             abs(model.temperature_tenths_c % 10));
  } else {
    snprintf(buffer, sizeof(buffer), "--");
  }
  canvas_.text(buffer, 130, 106, 1);
  canvas_.line(47, 124, 193, 124);

  canvas_.text("HUMID", 47, 134, 1);
  snprintf(buffer, sizeof(buffer), model.environment_valid ? "%u%% RH" : "--",
           model.humidity_percent);
  canvas_.text(buffer, 130, 134, 1);
  canvas_.line(47, 152, 193, 152);
  canvas_.centeredText("NOT BODY TEMPERATURE", 120, 174, 1);
}

void WatchUi::drawActivity(const WatchUiModel& model) {
  drawCardFrame("ACTIVITY", "GOAL 10000 STEPS");
  char buffer[24];
  snprintf(buffer, sizeof(buffer), "%lu",
           static_cast<unsigned long>(model.steps));
  canvas_.centeredText(buffer, 120, 53, 4);
  canvas_.centeredText("STEPS TODAY", 120, 88, 1);
  drawProgress(48, 111, 144,
               clampPercent(static_cast<int32_t>(model.steps / 100)));
  snprintf(buffer, sizeof(buffer), "%lu M",
           static_cast<unsigned long>(model.steps * 74 / 100));
  canvas_.text(buffer, 52, 139, 1);
  snprintf(buffer, sizeof(buffer), "%lu KCAL",
           static_cast<unsigned long>(model.steps * 41 / 1000));
  canvas_.text(buffer, 133, 139, 1);
  canvas_.line(120, 134, 120, 161);
  canvas_.centeredText("54 ACTIVE MIN", 120, 174, 1);
}

void WatchUi::drawSettings(const WatchUiModel& model) {
  drawCardFrame("SETTINGS", "PREV/NEXT  SELECT");
  const bool enabled[] = {model.bluetooth_connected, model.quiet_mode,
                          model.led_ring_on, model.vibration_on};
  const char* labels[] = {"BT", "QUIET", "LED", "VIBRA"};
  for (uint8_t index = 0; index < 4; ++index) {
    const int16_t x = index % 2 == 0 ? 60 : 121;
    const int16_t y = index < 2 ? 49 : 113;
    canvas_.rect(x, y, 58, 55);
    if (index == selected_setting_) {
      canvas_.rect(x + 2, y + 2, 54, 51);
    }
    canvas_.centeredText(labels[index], x + 29, y + 11, 1);
    canvas_.centeredText(enabled[index] ? "ON" : "OFF", x + 29, y + 32, 1);
  }
}
