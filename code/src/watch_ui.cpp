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

// Settings rows, in list order. The list scrolls: SETTING_VISIBLE rows are
// shown at the normal text size and the window follows the selection.
enum SettingRow : uint8_t {
  SETTING_BLUETOOTH,
  SETTING_QUIET,
  SETTING_LED_RING,
  SETTING_VIBRATION,
  SETTING_ALERT_TONE,
  SETTING_FIND_PHONE,
  SETTING_SECURE_LINK,
  SETTING_FORGET_PHONE,
  SETTING_COUNT,
};
constexpr uint8_t SETTING_VISIBLE = 5;

constexpr const char* TONE_NAMES[] = {
    "NONE", "CHIRP", "DOUBLE", "RISE", "FALL", "TRIPLE",
};
constexpr uint8_t TONE_NAME_COUNT = 6;

struct LocalTime {
  uint32_t day_index;
  uint8_t hour;
  uint8_t minute;
  uint8_t weekday;
};

LocalTime localTime(uint32_t local_epoch) {
  LocalTime value{};
  value.day_index = local_epoch / 86400UL;
  const uint32_t seconds = local_epoch % 86400UL;
  value.hour = static_cast<uint8_t>(seconds / 3600);
  value.minute = static_cast<uint8_t>((seconds / 60) % 60);
  value.weekday = static_cast<uint8_t>((value.day_index + 4) % 7);
  return value;
}

// Phone UTC epoch -> local epoch using the offset from the last TIME message.
uint32_t utcToLocal(uint32_t utc, int32_t offset) {
  const int64_t local = static_cast<int64_t>(utc) + offset;
  return local < 0 ? 0 : static_cast<uint32_t>(local);
}

// "9:05", or "TUE 9:05" when not today.
void formatLocalTime(char* buffer, size_t size, uint32_t local_epoch,
                     uint32_t now_local_epoch) {
  const LocalTime when = localTime(local_epoch);
  const uint8_t hour_12 =
      when.hour % 12 == 0 ? 12 : static_cast<uint8_t>(when.hour % 12);
  if (when.day_index == now_local_epoch / 86400UL) {
    snprintf(buffer, size, "%u:%02u", hour_12, when.minute);
  } else {
    snprintf(buffer, size, "%s %u:%02u", WEEKDAYS[when.weekday], hour_12,
             when.minute);
  }
}

// Mountain lake clock face. These mirror the L block of the "Mountain lake"
// face in tools/face_sim: tune them there, then copy the numbers here.
namespace lake {
// false restores the earlier clock face (WatchUi::drawClassicClock).
constexpr bool ENABLED = true;
constexpr int16_t BATTERY_Y = 10;
constexpr int16_t BATTERY_WIDTH = 28;
constexpr int16_t BATTERY_HEIGHT = 13;
constexpr int16_t DATE_TOP = 29;   // row where the date's capitals start
constexpr int16_t TIME_TOP = 48;   // row where the time's digits start
constexpr int16_t COLON_RADIUS = 5;
constexpr int16_t COLON_SPREAD = 10;  // digit middle to each dot's centre
constexpr int16_t COLON_NUDGE = 0;    // rows to raise both dots
constexpr int16_t EVENT_TOP = 99;
constexpr int16_t EVENT_MAX_WIDTH = 150;
constexpr int16_t SKY_TOP = 110;   // first row of the dusk and dawn bands
constexpr int16_t SHORE_Y = 164;   // waterline
constexpr float LEFT_PEAK_X = 70;
constexpr float LEFT_PEAK_HEIGHT = 30;
constexpr float LEFT_PEAK_LEFT = 20;
constexpr float LEFT_PEAK_RIGHT = 120;
constexpr float LEFT_SHOULDER_X = 104;
constexpr float LEFT_SHOULDER_HEIGHT = 9;
constexpr float RIGHT_PEAK_X = 150;
constexpr float RIGHT_PEAK_HEIGHT = 34;
constexpr float RIGHT_PEAK_LEFT = 96;
constexpr float RIGHT_PEAK_RIGHT = 214;
constexpr float REFLECTION = 0.6f;  // mirrored mountains, fraction as tall
constexpr int16_t SUN_RADIUS = 9;
constexpr float ARC_HEIGHT = 40;
constexpr float ARC_LEFT = 24;
constexpr float ARC_RIGHT = 216;
constexpr int16_t BANK_Y = 188;    // top of the shore band
constexpr int16_t ROW1_TOP = 194;  // temperature and humidity digits
constexpr int16_t ROW2_TOP = 212;  // altitude digits
constexpr int16_t READING_GAP = 10;
constexpr int16_t ICON_GAP = 6;
constexpr int16_t TWILIGHT_MINUTES = 30;  // each of four dusk / dawn steps
// The phone does not send sunrise and sunset yet, so they are fixed.
constexpr int16_t SUNRISE_MINUTES = 6 * 60;
constexpr int16_t SUNSET_MINUTES = 18 * 60;

constexpr int16_t STARS[][2] = {
    {22, 70},   {30, 100},  {14, 112},  {215, 64},  {224, 96},  {206, 112},
    {60, 116},  {100, 112}, {176, 114}, {36, 128},  {222, 126}, {18, 140},
    {130, 118}, {208, 142}, {46, 146},  {196, 150},
};
constexpr int16_t BIG_STARS[][2] = {{26, 86}, {214, 82}, {44, 114}, {186, 122}};
// x, rows below the waterline, length
constexpr int16_t RIPPLES[][3] = {
    {28, 12, 14}, {60, 20, 12}, {130, 21, 16}, {168, 18, 10}, {76, 15, 8},
};
// x at the shore, lean, height
constexpr int16_t REEDS[][3] = {
    {36, -3, 19}, {41, 1, 23}, {46, 4, 17}, {208, -2, 19}, {214, 3, 16},
};
}  // namespace lake

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

void MonochromeCanvas::fillCircle(int16_t center_x, int16_t center_y,
                                  int16_t radius, bool black) {
  if (radius < 0) {
    return;
  }
  for (int16_t dy = -radius; dy <= radius; ++dy) {
    const int16_t half = static_cast<int16_t>(
        sqrtf(static_cast<float>(radius * radius - dy * dy)));
    line(center_x - half, center_y + dy, center_x + half, center_y + dy,
         black);
  }
}

bool MonochromeCanvas::shadeAt(Shade shade, int16_t x, int16_t y) {
  switch (shade) {
    case Shade::White:
      return false;
    case Shade::Dither12:
      return ((x & 3) == 0 && (y & 3) == 0) || ((x & 3) == 2 && (y & 3) == 2);
    case Shade::Dither25:
      return (x & 1) == 0 && (y & 1) == 0;
    case Shade::Dither50:
      return ((x + y) & 1) == 0;
    case Shade::Dither75:
      return !((x & 1) == 0 && (y & 1) == 0);
    case Shade::Rows2:
      return (y & 1) == 0;
    case Shade::Rows3:
      return y % 3 == 0;
    case Shade::Rows3Inverse:
      return y % 3 != 0;
    case Shade::Black:
    default:
      return true;
  }
}

// Shaded fills are backgrounds, so like clear() they run to the edge of the
// framebuffer rather than stopping at the 236 px circle pixel() keeps to.
// Otherwise a fill that reaches the rim leaves a sliver of the cleared
// colour where the glass shows a little more than that circle.
void MonochromeCanvas::backgroundPixel(int16_t x, int16_t y, bool black) {
  if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) {
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

void MonochromeCanvas::shadeRect(int16_t x, int16_t y, int16_t width,
                                 int16_t height, Shade shade) {
  for (int16_t row = y; row < y + height; ++row) {
    for (int16_t column = x; column < x + width; ++column) {
      backgroundPixel(column, row, shadeAt(shade, column, row));
    }
  }
}

void MonochromeCanvas::fillPolygon(const PolygonPoint* points, uint8_t count,
                                   Shade shade) {
  if (points == nullptr || count < 3 || count > MAX_POLYGON_POINTS) {
    return;
  }
  float top = points[0].y;
  float bottom = points[0].y;
  for (uint8_t i = 1; i < count; ++i) {
    top = fminf(top, points[i].y);
    bottom = fmaxf(bottom, points[i].y);
  }
  const int16_t first_row =
      static_cast<int16_t>(fmaxf(0.0f, floorf(top)));
  const int16_t last_row =
      static_cast<int16_t>(fminf(HEIGHT - 1.0f, ceilf(bottom)));
  for (int16_t y = first_row; y <= last_row; ++y) {
    // Where the middle of this row crosses the polygon's edges.
    const float at = y + 0.5f;
    float crossings[MAX_POLYGON_POINTS];
    uint8_t found = 0;
    for (uint8_t i = 0; i < count; ++i) {
      const PolygonPoint& a = points[i];
      const PolygonPoint& b = points[(i + 1) % count];
      if ((a.y <= at && b.y > at) || (b.y <= at && a.y > at)) {
        crossings[found++] = a.x + (at - a.y) * (b.x - a.x) / (b.y - a.y);
      }
    }
    for (uint8_t i = 1; i < found; ++i) {  // insertion sort, left to right
      const float value = crossings[i];
      int8_t j = static_cast<int8_t>(i) - 1;
      while (j >= 0 && crossings[j] > value) {
        crossings[j + 1] = crossings[j];
        --j;
      }
      crossings[j + 1] = value;
    }
    for (uint8_t i = 0; i + 1 < found; i += 2) {
      const int16_t from = static_cast<int16_t>(
          fmaxf(0.0f, floorf(crossings[i] + 0.5f)));
      const int16_t to = static_cast<int16_t>(
          fminf(static_cast<float>(WIDTH), floorf(crossings[i + 1] + 0.5f)));
      for (int16_t x = from; x < to; ++x) {
        backgroundPixel(x, y, shadeAt(shade, x, y));
      }
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
  if (screen_ == WatchScreen::Notifications) {
    if (model.notification_count > 1) {
      notification_index_ = notification_index_ == 0
                                ? model.notification_count - 1
                                : notification_index_ - 1;
    }
    return WatchUiAction::None;
  }
  if (screen_ == WatchScreen::Settings) {
    selected_setting_ = selected_setting_ == 0 ? SETTING_COUNT - 1
                                               : selected_setting_ - 1;
    forget_armed_ = false;
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
  if (screen_ == WatchScreen::Notifications) {
    if (model.notification_count > 1) {
      notification_index_ = static_cast<uint8_t>(
          (notification_index_ + 1) % model.notification_count);
    }
    return WatchUiAction::None;
  }
  if (screen_ == WatchScreen::Settings) {
    selected_setting_ =
        static_cast<uint8_t>((selected_setting_ + 1) % SETTING_COUNT);
    forget_armed_ = false;
    return WatchUiAction::None;
  }
  if (screen_ == WatchScreen::Alarms) {
    selected_alarm_ = static_cast<uint8_t>((selected_alarm_ + 1) % 2);
  }
  return WatchUiAction::None;
}

void WatchUi::home() {
  forget_armed_ = false;
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
    forget_armed_ = false;
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
      // The application removes notificationIndex() and tells the phone.
      return model.notification_count > 0
                 ? WatchUiAction::NotificationDismissed
                 : WatchUiAction::None;
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
      return WatchUiAction::MediaPlayPause;
    case WatchScreen::Settings:
      if (selected_setting_ != SETTING_FORGET_PHONE) {
        forget_armed_ = false;
      }
      switch (selected_setting_) {
        case SETTING_BLUETOOTH:
          return WatchUiAction::BluetoothToggle;
        case SETTING_QUIET:
          model.quiet_mode = !model.quiet_mode;
          return WatchUiAction::PhoneSettingsChanged;
        case SETTING_LED_RING:
          model.led_ring_on = !model.led_ring_on;
          return WatchUiAction::SettingsChanged;
        case SETTING_VIBRATION:
          model.vibration_on = !model.vibration_on;
          return WatchUiAction::PhoneSettingsChanged;
        case SETTING_ALERT_TONE:
          model.alert_tone =
              static_cast<uint8_t>((model.alert_tone + 1) % TONE_NAME_COUNT);
          return WatchUiAction::AlertToneChanged;
        case SETTING_FIND_PHONE:
          return WatchUiAction::FindPhoneToggle;
        case SETTING_SECURE_LINK:
          if (!model.secure_link_available) {
            return WatchUiAction::None;
          }
          model.secure_link = !model.secure_link;
          return WatchUiAction::SecureLinkChanged;
        case SETTING_FORGET_PHONE:
          if (!model.phone_bonded) {
            forget_armed_ = false;
            return WatchUiAction::None;
          }
          if (!forget_armed_) {
            forget_armed_ = true;  // a second select confirms
            return WatchUiAction::None;
          }
          forget_armed_ = false;
          return WatchUiAction::ForgetPhone;
        default:
          return WatchUiAction::None;
      }
    default:
      return WatchUiAction::None;
  }
  return WatchUiAction::None;
}

void WatchUi::render(const WatchUiModel& model) {
  canvas_.clear();
  // An incoming call overlays whatever screen is current; when it ends the
  // previous screen is simply drawn again. A ringing alarm takes precedence.
  if (model.call_active && !model.alarm_ringing) {
    drawCall(model);
    return;
  }
  if (model.ota_screen != OtaScreen::None && !model.alarm_ringing) {
    drawOta(model);
    return;
  }
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

uint8_t WatchUi::notificationIndex() const {
  return notification_index_;
}

void WatchUi::setNotificationIndex(uint8_t index) {
  notification_index_ = index;
}

void WatchUi::clampNotificationIndex(uint8_t count) {
  if (notification_index_ >= count) {
    notification_index_ = count == 0 ? 0 : count - 1;
  }
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
  if (lake::ENABLED) {
    drawLakeClock(model);
  } else {
    drawClassicClock(model);
  }
}

// Mountain lake face: a still picture that changes through the day. The sun
// crosses the sky, the sky darkens around sunset, and the face inverts at
// night with a moon and stars. Designed and tuned in tools/face_sim (the
// "Mountain lake" face); the numbers in namespace lake mirror its L block.
void WatchUi::drawLakeClock(const WatchUiModel& model) {
  using Shade = MonochromeCanvas::Shade;
  using Point = MonochromeCanvas::PolygonPoint;
  using namespace lake;

  // ---- time of day: 0 day, 1-2 light dusk/dawn, 3-4 dark dusk/dawn, 5 night
  const int16_t now = static_cast<int16_t>(model.hour * 60 + model.minute);
  const int16_t rise = SUNRISE_MINUTES;
  const int16_t set = SUNSET_MINUTES;
  const int16_t T = TWILIGHT_MINUTES;
  uint8_t step = 0;
  if (now < rise) {
    const int16_t before = rise - now;
    step = before > 3 * T ? 5 : before > 2 * T ? 4 : before > T ? 3 : 2;
  } else if (now < rise + T) {
    step = 1;
  } else {
    const int16_t after = now - set;
    step = after >= 3 * T   ? 5
           : after >= 2 * T ? 4
           : after >= T     ? 3
           : after >= 0     ? 2
           : after >= -T    ? 1
                            : 0;
  }
  const bool night = step >= 3;  // inverted: white on black
  const bool ink = !night;       // "black" argument for foreground drawing
  const bool paper = night;      // and for drawing on the shore band
  canvas_.clear(night);
  const float S = SHORE_Y;
  const float B = BANK_Y;

  // ---- sky
  if (night) {
    for (const auto& star : STARS) {
      canvas_.pixel(star[0], star[1], false);
    }
    for (const auto& star : BIG_STARS) {
      canvas_.fillRect(star[0], star[1], 2, 2, false);
    }
  }
  if (step >= 1 && step <= 4) {
    static constexpr Shade BANDS[4][3] = {
        {Shade::White, Shade::Dither12, Shade::Dither25},
        {Shade::Dither12, Shade::Dither25, Shade::Dither50},
        {Shade::Dither75, Shade::Dither50, Shade::Dither25},
        {Shade::Black, Shade::Dither75, Shade::Dither50},
    };
    const int16_t band_height = (SHORE_Y - SKY_TOP + 2) / 3;
    for (uint8_t i = 0; i < 3; ++i) {
      const Shade shade = BANDS[step - 1][i];
      if (shade != (night ? Shade::Black : Shade::White)) {
        canvas_.shadeRect(0, SKY_TOP + i * band_height, 240, band_height,
                          shade);
      }
    }
  }

  // Sun by day, moon at full night, both on the same arc over the peaks.
  bool body_visible = false;
  int16_t body_x = 0;
  int16_t body_y = 0;
  const auto place_on_arc = [&](float fraction) {
    body_x = static_cast<int16_t>(
        floorf(ARC_LEFT + (ARC_RIGHT - ARC_LEFT) * fraction + 0.5f));
    body_y = static_cast<int16_t>(floorf(
        S - 2.0f - ARC_HEIGHT * sinf(3.14159265f * fraction) + 0.5f));
    body_visible = true;
  };
  if (!night && now >= rise && now <= set) {
    place_on_arc(static_cast<float>(now - rise) / (set - rise));
    canvas_.fillCircle(body_x, body_y, SUN_RADIUS, true);
    canvas_.fillCircle(body_x, body_y, SUN_RADIUS - 2, false);
  } else if (step == 5) {
    const int16_t night_length = 1440 - (set - rise);
    place_on_arc(static_cast<float>((now - set + 1440) % 1440) / night_length);
    canvas_.fillCircle(body_x, body_y, SUN_RADIUS, false);
    canvas_.fillCircle(body_x + 4, body_y - 3, SUN_RADIUS - 1, true);
  }

  // ---- hills, mountains, water
  const Point hills[] = {{0, S},        {0, S - 8},    {40, S - 16},
                         {80, S - 11},  {120, S - 12}, {170, S - 15},
                         {210, S - 11}, {240, S - 13}, {240, S}};
  canvas_.fillPolygon(hills, 9, night ? Shade::Dither75 : Shade::Dither25);
  const Point left_peak[] = {{LEFT_PEAK_LEFT, S},
                             {LEFT_PEAK_X, S - LEFT_PEAK_HEIGHT},
                             {LEFT_SHOULDER_X, S - LEFT_SHOULDER_HEIGHT},
                             {LEFT_PEAK_RIGHT, S}};
  canvas_.fillPolygon(left_peak, 4, Shade::Dither50);
  const Point right_peak[] = {{RIGHT_PEAK_LEFT, S},
                              {RIGHT_PEAK_X, S - RIGHT_PEAK_HEIGHT},
                              {RIGHT_PEAK_RIGHT, S}};
  canvas_.fillPolygon(right_peak, 3,
                      night ? Shade::Dither25 : Shade::Dither75);
  const auto snow_cap = [&](float x, float top) {
    const Point cap[] = {{x, top},          {x - 9, top + 7}, {x - 4, top + 6},
                         {x, top + 11},     {x + 5, top + 6}, {x + 9, top + 7}};
    canvas_.fillPolygon(cap, 6, Shade::White);
  };
  snow_cap(LEFT_PEAK_X, S - LEFT_PEAK_HEIGHT);
  snow_cap(RIGHT_PEAK_X, S - RIGHT_PEAK_HEIGHT);

  const Point left_mirror[] = {
      {LEFT_PEAK_LEFT, S},
      {LEFT_PEAK_X, S + LEFT_PEAK_HEIGHT * REFLECTION},
      {LEFT_SHOULDER_X, S + LEFT_SHOULDER_HEIGHT * REFLECTION},
      {LEFT_PEAK_RIGHT, S}};
  canvas_.fillPolygon(left_mirror, 4,
                      night ? Shade::Rows3Inverse : Shade::Rows3);
  const Point right_mirror[] = {
      {RIGHT_PEAK_LEFT, S},
      {RIGHT_PEAK_X, S + RIGHT_PEAK_HEIGHT * REFLECTION},
      {RIGHT_PEAK_RIGHT, S}};
  canvas_.fillPolygon(right_mirror, 3, Shade::Rows2);
  canvas_.line(0, SHORE_Y, 239, SHORE_Y, ink);
  for (const auto& ripple : RIPPLES) {
    canvas_.line(ripple[0], SHORE_Y + ripple[1], ripple[0] + ripple[2],
                 SHORE_Y + ripple[1], ink);
  }
  if (body_visible && body_y < SHORE_Y - 12) {
    // Sun or moon glitter on the water.
    static constexpr int16_t GLITTER[] = {12, 8, 14, 6};
    for (uint8_t i = 0; i < 4; ++i) {
      const int16_t y = SHORE_Y + 5 + i * 4;
      canvas_.line(body_x - GLITTER[i] / 2, y, body_x + GLITTER[i] / 2, y,
                   ink);
    }
  }

  // ---- shore and reeds
  const Point bank[] = {{0, B + 4},   {50, B - 2},  {90, B + 2},
                        {130, B},     {170, B - 2}, {205, B + 3},
                        {240, B - 1}, {240, 240},   {0, 240}};
  canvas_.fillPolygon(bank, 9, night ? Shade::White : Shade::Black);
  for (const auto& reed : REEDS) {
    canvas_.line(reed[0], BANK_Y + 2, reed[0] + reed[1], BANK_Y - reed[2],
                 ink);
    canvas_.fillRect(reed[0] + reed[1] - 1, BANK_Y - reed[2] - 8, 3, 9, ink);
  }

  // ---- readings and status symbols on the shore
  // Text is placed by the row where its digits start.
  const auto line_y = [](const BitmapFont& font, int16_t top) {
    return static_cast<int16_t>(
        top - (font.ascent +
               font.glyphs['0' - font.first_character].y_offset));
  };
  char temperature[8];
  char humidity[8];
  char altitude[10];
  if (model.environment_valid) {
    snprintf(temperature, sizeof(temperature), "%dC",
             model.temperature_tenths_c / 10);
    snprintf(humidity, sizeof(humidity), "%u%%", model.humidity_percent);
    snprintf(altitude, sizeof(altitude), "%dm",
             static_cast<int>(floorf(
                 44330.0f *
                     (1.0f - powf(model.pressure_hpa / 1013.25f, 0.1903f)) +
                 0.5f)));
  } else {
    snprintf(temperature, sizeof(temperature), "--C");
    snprintf(humidity, sizeof(humidity), "--%%");
    snprintf(altitude, sizeof(altitude), "--m");
  }
  const BitmapFont& reading = WatchAssets::LakeReading18;
  const int16_t reading_half = static_cast<int16_t>(
      (reading.glyphs['0' - reading.first_character].height + 1) / 2);
  const int16_t temperature_width =
      canvas_.fontTextWidth(reading, temperature);
  const int16_t row1_width = static_cast<int16_t>(
      temperature_width + READING_GAP +
      canvas_.fontTextWidth(reading, humidity));
  const int16_t row1_left = static_cast<int16_t>(120 - row1_width / 2);
  canvas_.fontText(reading, temperature, row1_left, line_y(reading, ROW1_TOP),
                   paper);
  canvas_.fontText(reading, humidity,
                   row1_left + temperature_width + READING_GAP,
                   line_y(reading, ROW1_TOP), paper);
  const int16_t row1_middle = ROW1_TOP + reading_half;
  if (model.bluetooth_connected) {
    canvas_.icon(WatchIcon::Bluetooth, row1_left - ICON_GAP - 8, row1_middle,
                 16, paper);
  }
  if (model.weekday_alarm_on || model.weekend_alarm_on ||
      model.next_alarm_utc != 0) {
    canvas_.icon(WatchIcon::Alarms, row1_left + row1_width + ICON_GAP + 8,
                 row1_middle, 16, paper);
  }

  canvas_.centeredFontText(reading, altitude, 120, line_y(reading, ROW2_TOP),
                           paper);
  const int16_t row2_middle = ROW2_TOP + reading_half;
  const int16_t half = static_cast<int16_t>(
      canvas_.fontTextWidth(reading, altitude) / 2 + ICON_GAP + 8);
  if (model.notification_count > 0) {
    canvas_.icon(WatchIcon::Notifications, 120 - half, row2_middle, 16,
                 paper);
  }
  if (model.vibration_on) {
    // No vibration icon in the asset set yet: a phone with shake marks.
    const int16_t cx = 120 + half;
    const int16_t cy = row2_middle;
    canvas_.rect(cx - 3, cy - 6, 7, 12, paper);
    canvas_.line(cx - 6, cy - 3, cx - 6, cy + 3, paper);
    canvas_.line(cx + 6, cy - 3, cx + 6, cy + 3, paper);
    canvas_.line(cx - 8, cy - 1, cx - 8, cy + 1, paper);
    canvas_.line(cx + 8, cy - 1, cx + 8, cy + 1, paper);
  }

  // ---- battery, date, time, next event
  const int16_t bx = 120 - BATTERY_WIDTH / 2;
  const int16_t by = BATTERY_Y;
  canvas_.rect(bx, by, BATTERY_WIDTH, BATTERY_HEIGHT, ink);
  canvas_.rect(bx + 1, by + 1, BATTERY_WIDTH - 2, BATTERY_HEIGHT - 2, ink);
  // Knock the corner pixels out: slightly rounded.
  canvas_.pixel(bx, by, !ink);
  canvas_.pixel(bx + BATTERY_WIDTH - 1, by, !ink);
  canvas_.pixel(bx, by + BATTERY_HEIGHT - 1, !ink);
  canvas_.pixel(bx + BATTERY_WIDTH - 1, by + BATTERY_HEIGHT - 1, !ink);
  canvas_.fillRect(
      bx + 2, by + 2,
      static_cast<int16_t>(((BATTERY_WIDTH - 4) *
                                clampPercent(model.battery_percent) +
                            50) /
                           100),
      BATTERY_HEIGHT - 4, ink);
  canvas_.fillRect(bx + BATTERY_WIDTH, by + 4, 2, BATTERY_HEIGHT - 8, ink);

  char buffer[48];
  snprintf(buffer, sizeof(buffer), "%s %u %s", WEEKDAYS[model.weekday % 7],
           model.day,
           MONTHS[(model.month >= 1 && model.month <= 12) ? model.month - 1
                                                          : 0]);
  canvas_.centeredFontText(WatchAssets::LakeDate20, buffer, 120,
                           line_y(WatchAssets::LakeDate20, DATE_TOP), ink);

  // The colon is two round dots centred on the middle of the digits, not
  // the font's glyph (square dots, and low on the baseline).
  const BitmapFont& clock = WatchAssets::LakeClock62;
  char hours[4];
  char minutes[4];
  snprintf(hours, sizeof(hours), "%02u", model.hour % 24);
  snprintf(minutes, sizeof(minutes), "%02u", model.minute % 60);
  const int16_t hours_width = canvas_.fontTextWidth(clock, hours);
  const int16_t colon_width = canvas_.fontTextWidth(clock, ":");
  const int16_t time_left = static_cast<int16_t>(
      120 - (hours_width + colon_width +
             canvas_.fontTextWidth(clock, minutes)) /
                2);
  canvas_.fontText(clock, hours, time_left, line_y(clock, TIME_TOP), ink);
  canvas_.fontText(clock, minutes, time_left + hours_width + colon_width,
                   line_y(clock, TIME_TOP), ink);
  const int16_t colon_x = time_left + hours_width + colon_width / 2;
  const int16_t colon_middle = static_cast<int16_t>(
      TIME_TOP + clock.glyphs['0' - clock.first_character].height / 2 -
      COLON_NUDGE);
  canvas_.fillCircle(colon_x, colon_middle - COLON_SPREAD, COLON_RADIUS, ink);
  canvas_.fillCircle(colon_x, colon_middle + COLON_SPREAD, COLON_RADIUS, ink);

  // What is coming up next (calendar or phone alarm), in 24-hour time.
  if (model.time_valid) {
    const uint32_t local = model.local_epoch;
    const uint32_t event =
        model.event_utc != 0
            ? utcToLocal(model.event_utc, model.utc_offset_seconds)
            : 0;
    const uint32_t alarm =
        model.next_alarm_utc != 0
            ? utcToLocal(model.next_alarm_utc, model.utc_offset_seconds)
            : 0;
    buffer[0] = '\0';
    if (event != 0 && event >= local && event - local <= 86400UL) {
      const LocalTime when = localTime(event);
      snprintf(buffer, sizeof(buffer), "%02u:%02u %s", when.hour, when.minute,
               model.event_title);
    } else if (alarm != 0 && alarm >= local && alarm - local <= 86400UL) {
      const LocalTime when = localTime(alarm);
      snprintf(buffer, sizeof(buffer), "%02u:%02u Alarm", when.hour,
               when.minute);
    }
    if (buffer[0] != '\0') {
      canvas_.centeredFittedFontText(
          WatchAssets::LakeEvent16, buffer, 120,
          line_y(WatchAssets::LakeEvent16, EVENT_TOP), EVENT_MAX_WIDTH, ink);
    }
  }
}

void WatchUi::drawClassicClock(const WatchUiModel& model) {
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

  // One quiet line for what is coming up next (calendar or phone alarm).
  drawUpcoming(model, 110);

  // Status belongs at the edge, not in the primary information hierarchy.
  canvas_.icon(WatchIcon::Battery, 28, 133, 16);
  if (model.weekday_alarm_on || model.weekend_alarm_on ||
      model.next_alarm_utc != 0) {
    canvas_.icon(WatchIcon::Alarms, 212, 133, 16);
  }
  if (model.notification_count > 0) {
    canvas_.icon(WatchIcon::Notifications, 28, 160, 16);
  }
  if (model.bluetooth_connected) {
    canvas_.icon(WatchIcon::Bluetooth, 212, 160, 16);
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
      if (model.notification_count > 0) {
        snprintf(status, sizeof(status), "%u NEW", model.notification_count);
      } else {
        snprintf(status, sizeof(status), "ALL CLEAR");
      }
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
               model.music_state == MusicState::Playing  ? "PLAYING"
               : model.music_state == MusicState::Paused ? "PAUSED"
                                                         : "NOTHING PLAYING");
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
      if (model.bluetooth_connected && model.phone_status_valid) {
        snprintf(status, sizeof(status), "PHONE %u%%",
                 model.phone_battery_percent);
      } else {
        snprintf(status, sizeof(status), "WATCH OPTIONS");
      }
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

void WatchUi::drawWrappedText(const BitmapFont& font, const char* text,
                              int16_t center_x, int16_t y, int16_t max_width,
                              uint8_t max_lines, int16_t line_height) {
  if (text == nullptr) {
    return;
  }
  const char* cursor = text;
  for (uint8_t line = 0; line < max_lines; ++line) {
    while (*cursor == ' ') {
      ++cursor;
    }
    if (*cursor == '\0') {
      return;
    }
    const int16_t line_y = static_cast<int16_t>(y + line * line_height);
    if (line + 1 == max_lines) {
      canvas_.centeredFittedFontText(font, cursor, center_x, line_y,
                                     max_width);
      return;
    }
    // Grow the line one character at a time, remembering the last word end
    // that still fits; a single over-long word is hard-broken.
    char buffer[64]{};
    size_t length = 0;
    size_t fit = 0;
    while (cursor[length] != '\0' && length < sizeof(buffer) - 1) {
      buffer[length] = cursor[length];
      buffer[length + 1] = '\0';
      if (canvas_.fontTextWidth(font, buffer) > max_width) {
        break;
      }
      ++length;
      if (cursor[length] == ' ' || cursor[length] == '\0') {
        fit = length;
      }
    }
    if (fit == 0) {
      fit = length > 0 ? length : 1;
    }
    buffer[fit] = '\0';
    canvas_.centeredFontText(font, buffer, center_x, line_y);
    cursor += fit;
  }
}

void WatchUi::drawNotifications(const WatchUiModel& model) {
  if (model.notification_count == 0) {
    drawCardFrame("MESSAGES", "");
    canvas_.centeredFontText(WatchAssets::Bold22, "NO MESSAGES", 120, 91);
    return;
  }
  const uint8_t index = notification_index_ < model.notification_count
                            ? notification_index_
                            : model.notification_count - 1;
  const WatchNotification& notification = model.notifications[index];
  drawCardFrame(notification.app[0] != '\0' ? notification.app : "MESSAGE",
                "DISMISS");
  int16_t body_y = 50;
  if (notification.title[0] != '\0') {
    canvas_.centeredFittedFontText(WatchAssets::Bold22, notification.title,
                                   120, 42, 176);
    body_y = 72;
  }
  drawWrappedText(WatchAssets::Ui16, notification.text, 120, body_y, 184,
                  body_y == 72 ? 4 : 5, 22);
  if (model.notification_count > 1) {
    char position[8];
    snprintf(position, sizeof(position), "%u/%u", index + 1,
             model.notification_count);
    canvas_.centeredFontText(WatchAssets::Ui16, position, 120, 172);
  }
}

void WatchUi::drawCall(const WatchUiModel& model) {
  canvas_.centeredFontText(WatchAssets::Ui16, "INCOMING CALL", 120, 30);
  canvas_.line(60, 56, 180, 56);
  drawWrappedText(WatchAssets::Bold22,
                  model.call_caller[0] != '\0' ? model.call_caller : "UNKNOWN",
                  120, 76, 196, 2, 28);
  canvas_.line(60, 146, 180, 146);
  canvas_.centeredFontText(WatchAssets::Ui16, "SELECT  REJECT", 120, 160);
  canvas_.centeredFontText(WatchAssets::Ui16, "BACK  HIDE", 120, 182);
}

void WatchUi::drawOta(const WatchUiModel& model) {
  char buffer[32];
  switch (model.ota_screen) {
    case OtaScreen::Confirm:
      canvas_.centeredFontText(WatchAssets::Ui16, "FIRMWARE UPDATE", 120, 30);
      canvas_.line(60, 56, 180, 56);
      canvas_.centeredFittedFontText(
          WatchAssets::Bold22,
          model.ota_version[0] != '\0' ? model.ota_version : "NEW VERSION",
          120, 70, 190);
      snprintf(buffer, sizeof(buffer), "%lu KB",
               static_cast<unsigned long>((model.ota_size + 1023UL) / 1024UL));
      canvas_.centeredFontText(WatchAssets::Ui16, buffer, 120, 104);
      canvas_.line(60, 146, 180, 146);
      canvas_.centeredFontText(WatchAssets::Ui16, "SELECT  INSTALL", 120, 160);
      canvas_.centeredFontText(WatchAssets::Ui16, "BACK  DECLINE", 120, 182);
      break;
    case OtaScreen::Progress:
      canvas_.centeredFontText(WatchAssets::Ui16, "RECEIVING UPDATE", 120, 34);
      snprintf(buffer, sizeof(buffer), "%u%%", model.ota_percent);
      canvas_.centeredFontText(WatchAssets::Bold36, buffer, 120, 74);
      drawProgress(48, 128, 144, model.ota_percent, 12);
      canvas_.centeredFontText(WatchAssets::Ui16, "KEEP PHONE CLOSE", 120,
                               160);
      break;
    case OtaScreen::Verifying:
      canvas_.centeredFontText(WatchAssets::Ui16, "FIRMWARE UPDATE", 120, 60);
      canvas_.centeredFontText(WatchAssets::Bold22, "VERIFYING", 120, 100);
      break;
    case OtaScreen::Installing:
      canvas_.centeredFontText(WatchAssets::Ui16, "INSTALLING UPDATE", 120,
                               60);
      canvas_.centeredFontText(WatchAssets::Bold22, "ABOUT 30 S", 120, 96);
      canvas_.centeredFontText(WatchAssets::Ui16, "DO NOT UNPLUG", 120, 140);
      break;
    case OtaScreen::Failed:
      canvas_.centeredFontText(WatchAssets::Ui16, "FIRMWARE UPDATE", 120, 60);
      canvas_.centeredFontText(WatchAssets::Bold22, "FAILED", 120, 96);
      canvas_.centeredFittedFontText(WatchAssets::Ui16, model.ota_detail, 120,
                                     132, 180);
      canvas_.centeredFontText(WatchAssets::Ui16, "ANY BUTTON", 120, 170);
      break;
    default:
      break;
  }
}

void WatchUi::drawUpcoming(const WatchUiModel& model, int16_t y) {
  if (!model.time_valid) {
    return;
  }
  const uint32_t now = model.local_epoch;
  const uint32_t event =
      model.event_utc != 0
          ? utcToLocal(model.event_utc, model.utc_offset_seconds)
          : 0;
  const uint32_t alarm =
      model.next_alarm_utc != 0
          ? utcToLocal(model.next_alarm_utc, model.utc_offset_seconds)
          : 0;
  char when[16];
  char line[48];
  if (event != 0 && event >= now && event - now <= 86400UL) {
    formatLocalTime(when, sizeof(when), event, now);
    snprintf(line, sizeof(line), "%s %s", when, model.event_title);
  } else if (alarm != 0 && alarm >= now && alarm - now <= 86400UL) {
    formatLocalTime(when, sizeof(when), alarm, now);
    snprintf(line, sizeof(line), "ALARM %s", when);
  } else {
    return;
  }
  // Narrow enough to clear the status icons at the ends of the divider.
  canvas_.centeredFittedFontText(WatchAssets::Ui16, line, 120, y, 136);
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
  canvas_.centeredFontText(WatchAssets::Ui16, "RINGS ON FINISH", 120, 165);
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
  } else if (model.next_alarm_utc != 0 && model.time_valid) {
    const uint32_t alarm =
        utcToLocal(model.next_alarm_utc, model.utc_offset_seconds);
    if (alarm >= model.local_epoch) {
      char when[16];
      char line[32];
      formatLocalTime(when, sizeof(when), alarm, model.local_epoch);
      snprintf(line, sizeof(line), "PHONE %s", when);
      canvas_.centeredFittedFontText(WatchAssets::Ui16, line, 120, 170, 150);
    }
  }
}

void WatchUi::drawMusic(const WatchUiModel& model) {
  const bool playing = model.music_state == MusicState::Playing;
  drawCardFrame(playing ? "PLAYING" : "MUSIC", "PLAY / PAUSE");
  if (model.music_state == MusicState::None) {
    canvas_.centeredFontText(WatchAssets::Bold22, "NOTHING PLAYING", 120, 66);
  } else {
    canvas_.centeredFittedFontText(WatchAssets::Bold22, model.music_title, 120,
                                   49, 174);
    canvas_.centeredFittedFontText(WatchAssets::Ui16, model.music_artist, 120,
                                   77, 180);
    const uint32_t duration = model.music_duration_seconds;
    const uint32_t position =
        duration != 0 && model.music_position_seconds > duration
            ? duration
            : model.music_position_seconds;
    const uint8_t progress =
        duration == 0 ? 0
                      : static_cast<uint8_t>(static_cast<uint64_t>(position) *
                                             100U / duration);
    drawProgress(42, 95, 156, progress);
    char time_value[12];
    snprintf(time_value, sizeof(time_value), "%lu:%02lu",
             static_cast<unsigned long>(position / 60),
             static_cast<unsigned long>(position % 60));
    canvas_.fontText(WatchAssets::Ui16, time_value, 42, 112);
    if (duration != 0) {
      snprintf(time_value, sizeof(time_value), "%lu:%02lu",
               static_cast<unsigned long>(duration / 60),
               static_cast<unsigned long>(duration % 60));
      canvas_.fontText(WatchAssets::Ui16, time_value,
                       static_cast<int16_t>(
                           198 - canvas_.fontTextWidth(WatchAssets::Ui16,
                                                       time_value)),
                       112);
    }
  }
  canvas_.text("|<", 54, 139, 2);
  canvas_.circle(120, 146, 22);
  if (playing) {
    canvas_.fillRect(113, 135, 5, 22);
    canvas_.fillRect(123, 135, 5, 22);
  } else {
    for (int16_t row = 0; row < 23; ++row) {
      canvas_.line(113, 135 + row, 113 + row * 3 / 4, 135 + row);
    }
  }
  canvas_.text(">|", 166, 139, 2);
  if (model.music_state != MusicState::None) {
    canvas_.centeredFontText(WatchAssets::Ui16, playing ? "PLAYING" : "PAUSED",
                             120, 178);
  }
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
  static constexpr const char* LABELS[SETTING_COUNT] = {
      "BLUETOOTH",  "QUIET",      "LED RING",    "VIBRATION",
      "ALERT TONE", "FIND PHONE", "SECURE LINK", "FORGET PHONE",
  };
  if (selected_setting_ >= SETTING_COUNT) {
    selected_setting_ = 0;
  }
  // Scroll just far enough to keep the selected row in the window.
  if (selected_setting_ < settings_top_) {
    settings_top_ = selected_setting_;
  } else if (selected_setting_ >= settings_top_ + SETTING_VISIBLE) {
    settings_top_ =
        static_cast<uint8_t>(selected_setting_ - SETTING_VISIBLE + 1);
  }
  if (settings_top_ > SETTING_COUNT - SETTING_VISIBLE) {
    settings_top_ = SETTING_COUNT - SETTING_VISIBLE;
  }

  char footer[16];
  snprintf(footer, sizeof(footer), "%u/%u  SELECT", selected_setting_ + 1,
           SETTING_COUNT);
  drawCardFrame("SETTINGS", footer);

  constexpr int16_t ROW_LEFT = 36;
  constexpr int16_t ROW_WIDTH = 168;
  constexpr int16_t LABEL_X = 42;
  constexpr int16_t VALUE_RIGHT = 198;
  constexpr int16_t TOGGLE_X = 170;
  for (uint8_t row = 0; row < SETTING_VISIBLE; ++row) {
    const uint8_t index = static_cast<uint8_t>(settings_top_ + row);
    const int16_t y = static_cast<int16_t>(40 + row * 24);
    canvas_.fontText(WatchAssets::Ui16, LABELS[index], LABEL_X, y + 3);
    const char* value = nullptr;
    switch (index) {
      case SETTING_BLUETOOTH:
        value = model.bluetooth_connected ? "LINK"
                : model.bluetooth_enabled ? "ON"
                                          : "OFF";
        break;
      case SETTING_QUIET:
        drawToggle(TOGGLE_X, y + 5, model.quiet_mode);
        break;
      case SETTING_LED_RING:
        drawToggle(TOGGLE_X, y + 5, model.led_ring_on);
        break;
      case SETTING_VIBRATION:
        drawToggle(TOGGLE_X, y + 5, model.vibration_on);
        break;
      case SETTING_ALERT_TONE:
        value = TONE_NAMES[model.alert_tone < TONE_NAME_COUNT
                               ? model.alert_tone
                               : 0];
        break;
      case SETTING_FIND_PHONE:
        drawToggle(TOGGLE_X, y + 5, model.find_phone_active);
        break;
      case SETTING_SECURE_LINK:
        if (model.secure_link_available) {
          drawToggle(TOGGLE_X, y + 5, model.secure_link);
        } else {
          value = "N/A";
        }
        break;
      case SETTING_FORGET_PHONE:
        value = !model.phone_bonded ? "--" : forget_armed_ ? "OK?" : "";
        break;
      default:
        break;
    }
    if (value != nullptr) {
      canvas_.fontText(
          WatchAssets::Ui16, value,
          static_cast<int16_t>(
              VALUE_RIGHT - canvas_.fontTextWidth(WatchAssets::Ui16, value)),
          y + 3);
    }
    if (inside_ && index == selected_setting_) {
      canvas_.rect(ROW_LEFT, y, ROW_WIDTH, 23);
    }
  }

  // Small arrows show that the list continues above / below the window.
  if (settings_top_ > 0) {
    for (int16_t row = 0; row < 4; ++row) {
      canvas_.line(120 - row, 35 + row, 120 + row, 35 + row);
    }
  }
  if (settings_top_ + SETTING_VISIBLE < SETTING_COUNT) {
    for (int16_t row = 0; row < 4; ++row) {
      canvas_.line(117 + row, 161 + row, 123 - row, 161 + row);
    }
  }

  char status[32];
  if (!model.bluetooth_connected) {
    snprintf(status, sizeof(status), "PHONE NOT CONNECTED");
  } else if (!model.phone_status_valid) {
    snprintf(status, sizeof(status), "PHONE CONNECTED");
  } else {
    snprintf(status, sizeof(status), "PHONE %u%%%s%s",
             model.phone_battery_percent,
             model.phone_charging ? " CHG" : "",
             model.phone_dnd ? " DND" : "");
  }
  canvas_.centeredFittedFontText(WatchAssets::Ui16, status, 120, 168, 184);
}
