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

const BitmapFont& fontForScale(uint8_t scale) {
  if (scale >= 8) {
    return WatchAssets::Clock64;
  }
  if (scale >= 5) {
    return WatchAssets::Bold36;
  }
  if (scale >= 3) {
    return WatchAssets::Bold22;
  }
  return WatchAssets::Ui16;
}

constexpr const char* SCREEN_LABELS[] = {
    "CLOCK", "MESSAGES", "TIMER", "ALARMS", "MUSIC",
    "WEATHER", "SENSORS", "ACTIVITY", "SETTINGS",
};

constexpr WatchIcon SCREEN_ICONS[] = {
    WatchIcon::Notifications, WatchIcon::Notifications, WatchIcon::Timer,
    WatchIcon::Alarms,        WatchIcon::Music,         WatchIcon::Weather,
    WatchIcon::Sensors,       WatchIcon::Activity,      WatchIcon::Settings,
};

WatchScreen launcherScreen(int16_t index) {
  constexpr int16_t first = static_cast<int16_t>(WatchScreen::Notifications);
  constexpr int16_t count = static_cast<int16_t>(WatchScreen::Count) - first;
  while (index < first) {
    index += count;
  }
  while (index >= first + count) {
    index -= count;
  }
  return static_cast<WatchScreen>(index);
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

int16_t MonochromeCanvas::textWidth(const char* value, uint8_t scale) const {
  return fontTextWidth(fontForScale(scale), value);
}

void MonochromeCanvas::text(const char* value, int16_t x, int16_t y,
                            uint8_t scale, bool black) {
  if (scale == 0) {
    return;
  }
  fontText(fontForScale(scale), value, x, y, black);
}

void MonochromeCanvas::centeredText(const char* value, int16_t center_x,
                                    int16_t y, uint8_t scale, bool black) {
  centeredFontText(fontForScale(scale), value, center_x, y, black);
}

int16_t MonochromeCanvas::fontTextWidth(const BitmapFont& font,
                                        const char* value) const {
  if (value == nullptr) {
    return 0;
  }
  int16_t width = 0;
  while (*value != '\0') {
    uint8_t character = static_cast<uint8_t>(*value++);
    if (character < font.first_character || character > font.last_character) {
      character = '?';
    }
    width += font.glyphs[character - font.first_character].x_advance;
  }
  return width;
}

void MonochromeCanvas::fontText(const BitmapFont& font, const char* value,
                                int16_t x, int16_t y, bool black) {
  if (value == nullptr) {
    return;
  }
  const int16_t baseline = y + font.ascent;
  while (*value != '\0') {
    uint8_t character = static_cast<uint8_t>(*value++);
    if (character < font.first_character || character > font.last_character) {
      character = '?';
    }
    const BitmapGlyph& glyph =
        font.glyphs[character - font.first_character];
    const uint8_t row_bytes = static_cast<uint8_t>((glyph.width + 7) / 8);
    for (uint8_t row = 0; row < glyph.height; ++row) {
      for (uint8_t column = 0; column < glyph.width; ++column) {
        const size_t byte_index = glyph.bitmap_offset +
                                  static_cast<size_t>(row) * row_bytes +
                                  column / 8;
        if ((font.bitmap[byte_index] & (0x80U >> (column & 7))) != 0) {
          pixel(x + glyph.x_offset + column,
                baseline + glyph.y_offset + row, black);
        }
      }
    }
    x += glyph.x_advance;
  }
}

void MonochromeCanvas::centeredFontText(const BitmapFont& font,
                                        const char* value, int16_t center_x,
                                        int16_t y, bool black) {
  fontText(font, value, center_x - fontTextWidth(font, value) / 2, y, black);
}

void MonochromeCanvas::centeredFittedFontText(const BitmapFont& font,
                                              const char* value,
                                              int16_t center_x, int16_t y,
                                              int16_t max_width, bool black) {
  if (value == nullptr || max_width <= 0) {
    return;
  }
  if (fontTextWidth(font, value) <= max_width) {
    centeredFontText(font, value, center_x, y, black);
    return;
  }

  char fitted[48]{};
  size_t length = 0;
  while (value[length] != '\0' && length < sizeof(fitted) - 4) {
    fitted[length] = value[length];
    fitted[length + 1] = '\0';
    char candidate[48]{};
    snprintf(candidate, sizeof(candidate), "%s...", fitted);
    if (fontTextWidth(font, candidate) > max_width) {
      fitted[length] = '\0';
      break;
    }
    ++length;
  }
  while (length > 0 && fitted[length - 1] == ' ') {
    fitted[--length] = '\0';
  }
  strncat(fitted, "...", sizeof(fitted) - strlen(fitted) - 1);
  centeredFontText(font, fitted, center_x, y, black);
}

void MonochromeCanvas::icon(WatchIcon value, int16_t center_x,
                            int16_t center_y, uint8_t size, bool black) {
  if (static_cast<uint8_t>(value) >=
      static_cast<uint8_t>(WatchIcon::Count)) {
    return;
  }
  const BitmapIcon& asset = WatchAssets::icon(value, size);
  const uint8_t* bitmap = WatchAssets::iconBitmap();
  const uint8_t row_bytes = static_cast<uint8_t>((asset.width + 7) / 8);
  const int16_t left = center_x - asset.width / 2;
  const int16_t top = center_y - asset.height / 2;
  for (uint8_t row = 0; row < asset.height; ++row) {
    for (uint8_t column = 0; column < asset.width; ++column) {
      const size_t byte_index = asset.bitmap_offset +
                                static_cast<size_t>(row) * row_bytes +
                                column / 8;
      if ((bitmap[byte_index] & (0x80U >> (column & 7))) != 0) {
        pixel(left + column, top + row, black);
      }
    }
  }
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
  if (screen_ != WatchScreen::Clock && !inside_) {
    drawLauncher(model);
    return;
  }
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
  const uint8_t index = static_cast<uint8_t>(screen_);
  return index < static_cast<uint8_t>(WatchScreen::Count)
             ? NAMES[index]
             : "clock";
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
  snprintf(buffer, sizeof(buffer), "%s  %u %s", weekday, model.day, month);
  canvas_.centeredFontText(WatchAssets::Bold22, buffer, 120, 18);

  const uint8_t hour_12 =
      model.hour % 12 == 0 ? 12 : static_cast<uint8_t>(model.hour % 12);
  snprintf(buffer, sizeof(buffer), "%u:%02u", hour_12, model.minute);
  const int16_t time_width =
      canvas_.fontTextWidth(WatchAssets::Clock64, buffer);
  canvas_.centeredFontText(WatchAssets::Clock64, buffer, 120, 47);
  canvas_.fontText(WatchAssets::Ui16, model.hour < 12 ? "AM" : "PM",
                   static_cast<int16_t>(122 + time_width / 2), 87);

  // Status belongs at the edge, not in the primary information hierarchy.
  canvas_.icon(WatchIcon::Battery, 28, 133, 16);
  if (model.weekday_alarm_on || model.weekend_alarm_on) {
    canvas_.icon(WatchIcon::Alarms, 212, 133, 16);
  }
  if (model.notification_present) {
    canvas_.icon(WatchIcon::Notifications, 28, 160, 16);
  }
  canvas_.line(48, 133, 192, 133);

  // Weather and movement are useful at a glance, but deliberately secondary.
  canvas_.icon(WatchIcon::Weather, 78, 157, 16);
  if (model.phone_weather_valid) {
    snprintf(buffer, sizeof(buffer), "%dC", model.forecast_temperature_c);
  } else if (model.environment_valid) {
    snprintf(buffer, sizeof(buffer), "%dC",
             model.temperature_tenths_c / 10);
  } else {
    snprintf(buffer, sizeof(buffer), "--C");
  }
  canvas_.centeredFontText(WatchAssets::Bold22, buffer, 78, 169);
  canvas_.centeredFittedFontText(
      WatchAssets::Ui16,
      model.phone_weather_valid ? model.weather_condition : "WEATHER", 78,
      197, 72);

  canvas_.icon(WatchIcon::Activity, 162, 157, 16);
  snprintf(buffer, sizeof(buffer), "%lu",
           static_cast<unsigned long>(model.steps));
  canvas_.centeredFittedFontText(WatchAssets::Bold22, buffer, 162, 169, 72);
  canvas_.centeredFontText(WatchAssets::Ui16, "STEPS", 162, 197);
}

void WatchUi::drawLauncher(const WatchUiModel& model) {
  int16_t current = static_cast<int16_t>(screen_);
  if (current < static_cast<int16_t>(WatchScreen::Notifications) ||
      current >= static_cast<int16_t>(WatchScreen::Count)) {
    screen_ = WatchScreen::Notifications;
    current = static_cast<int16_t>(screen_);
  }

  // The settled e-paper frame implies a rolling cylinder: distant entries are
  // small and deliberately clipped by the round panel, while the selected app
  // is large and centered. There is no animation on the panel.
  const WatchScreen two_before = launcherScreen(current - 2);
  const WatchScreen before = launcherScreen(current - 1);
  const WatchScreen after = launcherScreen(current + 1);
  const WatchScreen two_after = launcherScreen(current + 2);
  canvas_.centeredFontText(
      WatchAssets::Ui16, SCREEN_LABELS[static_cast<uint8_t>(two_before)], 120,
      -5);
  canvas_.centeredFontText(
      WatchAssets::Ui16, SCREEN_LABELS[static_cast<uint8_t>(before)], 120, 30);
  canvas_.centeredFontText(
      WatchAssets::Ui16, SCREEN_LABELS[static_cast<uint8_t>(after)], 120, 190);
  canvas_.centeredFontText(
      WatchAssets::Ui16, SCREEN_LABELS[static_cast<uint8_t>(two_after)], 120,
      226);

  canvas_.icon(SCREEN_ICONS[static_cast<uint8_t>(screen_)], 120, 88, 28);
  const char* selected = SCREEN_LABELS[static_cast<uint8_t>(screen_)];
  canvas_.centeredFontText(WatchAssets::Bold36, selected, 120, 111);

  char status[32] = {};
  switch (screen_) {
    case WatchScreen::Notifications:
      snprintf(status, sizeof(status), "%s",
               model.notification_present ? "1 NEW" : "ALL CLEAR");
      break;
    case WatchScreen::Timer:
      snprintf(status, sizeof(status), "%lu MIN%s",
               static_cast<unsigned long>(model.timer_remaining_seconds / 60),
               model.timer_running ? " RUNNING" : "");
      break;
    case WatchScreen::Alarms:
      snprintf(status, sizeof(status), "%02u:%02u  %s", model.alarm_hour,
               model.alarm_minute, model.weekday_alarm_on ? "ON" : "OFF");
      break;
    case WatchScreen::Music:
      snprintf(status, sizeof(status), "%s",
               model.music_playing ? "PLAYING" : "PAUSED");
      break;
    case WatchScreen::Weather:
      snprintf(status, sizeof(status), "%s",
               model.phone_weather_valid ? model.weather_condition
                                         : "PHONE DATA");
      break;
    case WatchScreen::Sensors:
      snprintf(status, sizeof(status), "LIVE READINGS");
      break;
    case WatchScreen::Activity:
      snprintf(status, sizeof(status), "%lu STEPS",
               static_cast<unsigned long>(model.steps));
      break;
    case WatchScreen::Settings:
      snprintf(status, sizeof(status), "WATCH OPTIONS");
      break;
    default:
      break;
  }
  canvas_.centeredFittedFontText(WatchAssets::Ui16, status, 120, 157, 180);
}

void WatchUi::drawCardFrame(const char* title, const char* footer) {
  canvas_.centeredFittedFontText(WatchAssets::Ui16, title, 120, 18, 140);
  if (inside_) {
    canvas_.centeredFittedFontText(WatchAssets::Ui16, footer, 120, 204, 150);
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

void WatchUi::drawNotifications(const WatchUiModel& model) {
  drawCardFrame("MESSAGE 2 MIN", "DISMISS");
  if (!model.notification_present) {
    canvas_.centeredFontText(WatchAssets::Bold22, "NO MESSAGES", 120, 91);
    return;
  }
  canvas_.centeredFittedFontText(WatchAssets::Bold22,
                                model.notification_sender, 120, 50, 170);
  canvas_.centeredFittedFontText(WatchAssets::Ui16,
                                model.notification_line_1, 120, 82, 188);
  canvas_.centeredFittedFontText(WatchAssets::Ui16,
                                model.notification_line_2, 120, 106, 188);
  canvas_.line(43, 136, 197, 136);
  canvas_.centeredFontText(WatchAssets::Ui16, "PHONE NOTIFICATION", 120, 151);
}

void WatchUi::drawTimer(const WatchUiModel& model) {
  drawCardFrame("TIMER", "START / STOP");
  char buffer[12];
  const uint32_t minutes = model.timer_remaining_seconds / 60;
  const uint32_t seconds = model.timer_remaining_seconds % 60;
  snprintf(buffer, sizeof(buffer), "%02lu:%02lu",
           static_cast<unsigned long>(minutes),
           static_cast<unsigned long>(seconds));
  canvas_.centeredFontText(WatchAssets::Clock64, buffer, 120, 51);
  canvas_.centeredFontText(WatchAssets::Ui16,
                           model.timer_running ? "RUNNING" : "READY", 120,
                           127);
  canvas_.fillRect(57, 151, 126, 2);
  canvas_.centeredFontText(WatchAssets::Ui16, "VIBRATE ON FINISH", 120, 165);
}

void WatchUi::drawAlarms(const WatchUiModel& model) {
  drawCardFrame("ALARMS", "TOGGLE");
  char alarm[8];
  snprintf(alarm, sizeof(alarm), "%02u:%02u", model.alarm_hour,
           model.alarm_minute);
  canvas_.fontText(WatchAssets::Bold22, alarm, 47, 53);
  canvas_.fontText(WatchAssets::Ui16, "WEEKDAYS", 47, 80);
  drawToggle(163, 61, model.weekday_alarm_on);
  if (inside_ && selected_alarm_ == 0) {
    canvas_.rect(40, 50, 160, 45);
  }
  canvas_.line(42, 101, 198, 101);
  canvas_.fontText(WatchAssets::Bold22, "09:00", 47, 114);
  canvas_.fontText(WatchAssets::Ui16, "WEEKEND", 47, 141);
  drawToggle(163, 122, model.weekend_alarm_on);
  if (inside_ && selected_alarm_ == 1) {
    canvas_.rect(40, 109, 160, 45);
  }
  if (model.alarm_ringing) {
    canvas_.centeredText("RINGING", 120, 174, 2);
  }
}

void WatchUi::drawMusic(const WatchUiModel& model) {
  drawCardFrame("PLAYING", "PLAY / PAUSE");
  canvas_.centeredFittedFontText(WatchAssets::Bold22, model.music_title, 120,
                                49, 174);
  canvas_.centeredFittedFontText(WatchAssets::Ui16, model.music_artist, 120,
                                77, 180);
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
  canvas_.fontText(WatchAssets::Ui16, time_value, 42, 112);
  snprintf(time_value, sizeof(time_value), "%u:%02u",
           model.music_duration_seconds / 60,
           model.music_duration_seconds % 60);
  canvas_.fontText(WatchAssets::Ui16, time_value, 168, 112);
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
  canvas_.centeredFontText(WatchAssets::Ui16,
                           model.music_playing ? "PLAYING 1/2" : "PAUSED 1/2",
                           120, 178);
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
  canvas_.centeredFittedFontText(WatchAssets::Ui16, model.weather_condition,
                                120, 94, 178);
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
  drawCardFrame("SENSORS", "AMBIENT AIR");
  char buffer[24];
  canvas_.text("PRESS", 47, 58, 1);
  snprintf(buffer, sizeof(buffer), model.environment_valid ? "%u HPA" : "--",
           model.pressure_hpa);
  canvas_.text(buffer, 130, 58, 1);
  canvas_.line(47, 79, 193, 79);

  canvas_.text("TEMP", 47, 93, 1);
  if (model.environment_valid) {
    snprintf(buffer, sizeof(buffer), "%d.%d C",
             model.temperature_tenths_c / 10,
             abs(model.temperature_tenths_c % 10));
  } else {
    snprintf(buffer, sizeof(buffer), "--");
  }
  canvas_.text(buffer, 130, 93, 1);
  canvas_.line(47, 114, 193, 114);

  canvas_.text("HUMID", 47, 128, 1);
  snprintf(buffer, sizeof(buffer), model.environment_valid ? "%u%% RH" : "--",
           model.humidity_percent);
  canvas_.text(buffer, 130, 128, 1);
  canvas_.line(47, 149, 193, 149);
}

void WatchUi::drawActivity(const WatchUiModel& model) {
  drawCardFrame("ACTIVITY", "10K GOAL");
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
  drawCardFrame("SETTINGS", "SELECT");
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
