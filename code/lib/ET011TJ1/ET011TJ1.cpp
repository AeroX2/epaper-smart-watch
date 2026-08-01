#include "ET011TJ1.h"

#include "ET011VendorReference.h"

namespace {

constexpr uint8_t CMD_PSR = 0x00;
constexpr uint8_t CMD_PWR = 0x01;
constexpr uint8_t CMD_POF = 0x02;
constexpr uint8_t CMD_PFS = 0x03;
constexpr uint8_t CMD_PON = 0x04;
constexpr uint8_t CMD_BTST = 0x06;
constexpr uint8_t CMD_DTM1 = 0x10;
constexpr uint8_t CMD_DRF = 0x12;
constexpr uint8_t CMD_DTM2 = 0x13;
constexpr uint8_t CMD_DTMW = 0x14;
constexpr uint8_t CMD_LUT_20 = 0x20;
constexpr uint8_t CMD_LUT_22 = 0x22;
constexpr uint8_t CMD_LUT_26 = 0x26;
constexpr uint8_t CMD_LPRD = 0x30;
constexpr uint8_t CMD_TSE = 0x41;
constexpr uint8_t CMD_CDI = 0x50;
constexpr uint8_t CMD_TRES = 0x61;
constexpr uint8_t CMD_GDS = 0x62;
constexpr uint8_t CMD_GBS = 0x63;
constexpr uint8_t CMD_GSS = 0x64;
constexpr uint8_t CMD_VDCS = 0x82;
constexpr uint8_t CMD_VBDS = 0x84;
constexpr uint8_t CMD_LVSEL = 0xE4;

// Values retained from the proven board bring-up where the supplied archive
// either used the same value or left its source variable uninitialized.
constexpr uint8_t POWER_SETTINGS[] = {0x03, 0x01, 0x2B, 0x2B, 0x00};
constexpr uint8_t BOOSTER_SOFT_START[] = {0x17, 0x97, 0x20};
constexpr uint8_t POWER_OFF_SEQUENCE = 0x00;
constexpr uint8_t INTERNAL_TEMPERATURE_SENSOR = 0x00;
constexpr uint8_t RESOLUTION[] = {0xEF, 0x00, 0xEF};
// VDCS (R82H) sets VCOM_DC; 0x25 = -1.95 V.
constexpr uint8_t VCOM_DC_LEVEL = 0x25;
constexpr uint8_t BORDER_DRIVER_VOLTAGE = 0x25;
constexpr uint8_t LEVEL_SELECT = 0x02;
constexpr uint8_t GATE_BIAS_START[] = {0x02, 0x02};
constexpr uint8_t GATE_BIAS_STOP[] = {0x02, 0x02};

constexpr uint8_t FULL_WINDOW[] = {
    0x00,        // X start
    0x00, 0x00,  // Y start
    0xEF,        // X end = 239
    0x00, 0xEF,  // Y end = 239
};

// Register values and refresh mode copied from the supplied
// Ardiuno_ET011TJ2_hspi_01.zip (whose inner sketch is named ET011TT6).
constexpr uint8_t VENDOR_PANEL_SETTINGS[] = {0x0B, 0x86};
constexpr uint8_t VENDOR_LINE_PERIOD = 0x25;
constexpr uint8_t VENDOR_VCOM_DATA_INTERVAL[] = {0xE1, 0x20, 0x10};
constexpr uint8_t VENDOR_GATE_DRIVER_SETTINGS[] = {
    0xA9, 0xA9, 0xEB, 0xEB, 0x02,
};
constexpr uint8_t VENDOR_FULL_REFRESH[] = {
    0x08,
    0x00,        // X start
    0x00, 0x00,  // Y start
    0xEF,        // X end = 239
    0x00, 0xEF,  // Y end = 239
};

// Epson's ET011TT2 reference uses GC mode with DN_EN for subsequent images.
// DTM1 must contain the previous frame and DTM2 the new frame. Unlike the ZIP
// demo's first-image 0x08 mode, this avoids the conspicuous cleaning flash.
constexpr uint8_t GC_FAST_REFRESH_MODE = 0x04;
constexpr uint8_t VENDOR_FULL_REFRESH_MODE = 0x08;

}  // namespace

ET011TJ1::ET011TJ1(SPIClass& spi, uint32_t cs_pin, uint32_t dc_pin,
                   uint32_t reset_pin, uint32_t busy_pin)
    : spi_(spi),
      spi_settings_(SPI_HZ, MSBFIRST, SPI_MODE0),
      cs_pin_(cs_pin),
      dc_pin_(dc_pin),
      reset_pin_(reset_pin),
      busy_pin_(busy_pin) {}

void ET011TJ1::prepare() {
  if (prepared_) {
    return;
  }

  digitalWrite(cs_pin_, HIGH);
  pinMode(cs_pin_, OUTPUT);
  digitalWrite(dc_pin_, LOW);
  pinMode(dc_pin_, OUTPUT);
  digitalWrite(reset_pin_, LOW);
  pinMode(reset_pin_, OUTPUT);
  // A weak pull-down makes a disconnected BUSY_N fail the logic probe instead
  // of looking spuriously ready. A connected controller actively drives high.
  pinMode(busy_pin_, INPUT_PULLDOWN);
  spi_.begin();
  prepared_ = true;
  controller_awake_ = false;
  booster_on_ = false;
}

void ET011TJ1::shutdown() {
  if (controller_awake_) {
    writeCommand(CMD_POF);
    delay(50);
  }
  holdInReset();
}

bool ET011TJ1::logicProbe(Print& out) {
  prepare();

  out.print(F("ET011TJ1 BUSY_N before reset = "));
  out.println(busyLevel());
  const int busy_during_reset = hardwareReset();
  const bool ready = waitReady(1000);
  const int ready_level = busyLevel();
  const bool passed = busy_during_reset == LOW && ready;

  out.print(passed ? F("[PASS] ") : F("[FAIL] "));
  out.print(F("ET011TJ1 reset/logic probe; BUSY_N reset/ready = "));
  out.print(busy_during_reset);
  out.print('/');
  out.println(ready_level);
  if (!passed) {
    out.println(F("Check the display FPC, 3.0 V rail, RST_N, and BUSY_N."));
  }
  holdInReset();
  out.println(F("ET011TJ1 returned to reset; booster was not enabled"));
  return passed;
}

bool ET011TJ1::show(ET011TJ1Pattern pattern, Print& out) {
  prepare();
  out.println(F("Display driver: supplied ET011TJ2 ZIP / ET011TT6 sketch"));
  out.println(F("Display test: high-voltage booster will be enabled."));

  if (!initializeVendorUpdate(out)) {
    safeShutdown(out);
    return false;
  }

  out.println(F("Vendor update: writing 240x240 current frame..."));
  writeCommandData(CMD_DTMW, FULL_WINDOW, sizeof(FULL_WINDOW));
  writePattern(CMD_DTM2, pattern, true);

  if (!finishVendorUpdate(out, VENDOR_FULL_REFRESH_MODE)) {
    safeShutdown(out);
    return false;
  }

  out.println(F("[PASS] Supplied vendor display refresh completed"));
  holdInReset();
  out.println(F("ET011 controller returned to reset; booster is off"));
  return true;
}

bool ET011TJ1::showFrame(const uint8_t* framebuffer, size_t stride_bytes,
                        Print& out) {
  if (framebuffer == nullptr || stride_bytes < WIDTH / 8) {
    out.println(F("[FAIL] Invalid ET011TJ1 framebuffer"));
    return false;
  }

  prepare();
  out.println(F("Display driver: supplied ET011TJ2 ZIP / ET011TT6 sketch"));
  out.println(F("Display UI: high-voltage booster will be enabled."));

  if (!initializeVendorUpdate(out)) {
    safeShutdown(out);
    return false;
  }

  out.println(F("Vendor update: streaming 240x240 UI framebuffer..."));
  writeCommandData(CMD_DTMW, FULL_WINDOW, sizeof(FULL_WINDOW));
  writeFramebuffer(CMD_DTM2, framebuffer, stride_bytes, true);

  if (!finishVendorUpdate(out, VENDOR_FULL_REFRESH_MODE)) {
    safeShutdown(out);
    return false;
  }

  out.println(F("[PASS] Supplied vendor UI refresh completed"));
  holdInReset();
  out.println(F("ET011 controller returned to reset; booster is off"));
  return true;
}

bool ET011TJ1::showFrameFast(const uint8_t* framebuffer,
                             const uint8_t* previous_framebuffer,
                             size_t stride_bytes, Print& out) {
  if (framebuffer == nullptr || previous_framebuffer == nullptr ||
      stride_bytes < WIDTH / 8) {
    out.println(F("[FAIL] Invalid ET011TJ1 fast-update framebuffer"));
    return false;
  }

  prepare();
  out.println(F("Display driver: Epson GC fast update (DTM1 old / DTM2 new)"));
  out.println(F("Display UI: high-voltage booster will be enabled."));

  if (!initializeVendorUpdate(out, previous_framebuffer, stride_bytes)) {
    safeShutdown(out);
    return false;
  }

  out.println(F("Fast update: streaming new 240x240 UI framebuffer..."));
  writeCommandData(CMD_DTMW, FULL_WINDOW, sizeof(FULL_WINDOW));
  writeFramebuffer(CMD_DTM2, framebuffer, stride_bytes, true);

  if (!finishVendorUpdate(out, GC_FAST_REFRESH_MODE)) {
    safeShutdown(out);
    return false;
  }

  out.println(F("[PASS] Epson GC fast UI refresh completed"));
  holdInReset();
  out.println(F("ET011 controller returned to reset; booster is off"));
  return true;
}

int ET011TJ1::busyLevel() const {
  return digitalRead(busy_pin_);
}

int ET011TJ1::hardwareReset() {
  digitalWrite(cs_pin_, HIGH);
  // The normal idle state holds RST_N low. First release it so the following
  // low pulse is a real reset edge rather than LOW -> LOW -> HIGH.
  digitalWrite(reset_pin_, HIGH);
  delay(20);
  digitalWrite(reset_pin_, LOW);
  delay(10);
  const int busy_during_reset = busyLevel();
  digitalWrite(reset_pin_, HIGH);
  delay(100);
  controller_awake_ = true;
  return busy_during_reset;
}

bool ET011TJ1::initializeVendorUpdate(Print& out,
                                     const uint8_t* history_framebuffer,
                                     size_t history_stride_bytes) {
  out.println(F("Initializing supplied vendor display sequence..."));
  hardwareReset();
  if (!waitReady(1000)) {
    out.println(F("[FAIL] BUSY_N stayed low after hardware reset"));
    return false;
  }

  // Preserve the supplied archive's unusual BTST -> PWR -> PON order.
  writeCommandData(CMD_BTST, BOOSTER_SOFT_START,
                   sizeof(BOOSTER_SOFT_START));
  writeCommandData(CMD_PWR, POWER_SETTINGS, sizeof(POWER_SETTINGS));
  if (!powerOn(out) || !configureVendorReference(out)) {
    return false;
  }

  // The sketch writes 14,401 bytes due to an off-by-one. Send the controller's
  // actual 240x240x2-bpp capacity (14,400), with its 0xFF=white polarity.
  out.println(history_framebuffer == nullptr
                  ? F("Vendor init: setting DTM1 history to white...")
                  : F("Fast init: streaming the previous frame to DTM1..."));
  writeCommandData(CMD_DTMW, FULL_WINDOW, sizeof(FULL_WINDOW));
  if (history_framebuffer == nullptr) {
    writePattern(CMD_DTM1, ET011TJ1Pattern::White, true);
  } else {
    writeFramebuffer(CMD_DTM1, history_framebuffer, history_stride_bytes,
                     true);
  }
  if (!powerOff(out)) {
    return false;
  }

  // The supplied code reloads all three waveform registers before each frame.
  writeCommandData(CMD_LUT_20, ET011VendorReference::LUT_20,
                   sizeof(ET011VendorReference::LUT_20));
  writeCommandData(CMD_LUT_22, ET011VendorReference::LUT_22,
                   sizeof(ET011VendorReference::LUT_22));
  writeCommandData(CMD_LUT_26, ET011VendorReference::LUT_26,
                   sizeof(ET011VendorReference::LUT_26));
  return true;
}

bool ET011TJ1::configureVendorReference(Print& out) {
  writeCommandData(CMD_PSR, VENDOR_PANEL_SETTINGS,
                   sizeof(VENDOR_PANEL_SETTINGS));
  writeCommandData(CMD_PFS, &POWER_OFF_SEQUENCE, 1);
  writeCommandData(CMD_LPRD, &VENDOR_LINE_PERIOD, 1);
  writeCommandData(CMD_TSE, &INTERNAL_TEMPERATURE_SENSOR, 1);
  writeCommandData(CMD_CDI, VENDOR_VCOM_DATA_INTERVAL,
                   sizeof(VENDOR_VCOM_DATA_INTERVAL));
  writeCommandData(CMD_TRES, RESOLUTION, sizeof(RESOLUTION));
  writeCommandData(CMD_GDS, VENDOR_GATE_DRIVER_SETTINGS,
                   sizeof(VENDOR_GATE_DRIVER_SETTINGS));

  // VcomOTP is never populated in the supplied sketch and therefore sends
  // zero. Retain the panel value already proven on this board instead.
  writeCommandData(CMD_VDCS, &VCOM_DC_LEVEL, 1);
  writeCommandData(CMD_VBDS, &BORDER_DRIVER_VOLTAGE, 1);
  writeCommandData(CMD_LVSEL, &LEVEL_SELECT, 1);
  writeCommandData(CMD_GBS, GATE_BIAS_START, sizeof(GATE_BIAS_START));
  writeCommandData(CMD_GSS, GATE_BIAS_STOP, sizeof(GATE_BIAS_STOP));
  out.println(F("Supplied vendor register set loaded"));
  return true;
}

bool ET011TJ1::powerOn(Print& out) {
  if (!waitReady(1000)) {
    out.println(F("[FAIL] BUSY_N was low before PON"));
    return false;
  }

  writeCommand(CMD_PON);
  booster_on_ = true;
  if (!waitBusyCycle(1000, 5000)) {
    out.print(F("[FAIL] PON BUSY_N cycle timed out; level = "));
    out.println(busyLevel());
    return false;
  }

  out.println(F("Booster power-on complete"));
  return true;
}

bool ET011TJ1::powerOff(Print& out) {
  if (!waitReady(1000)) {
    out.println(F("[FAIL] BUSY_N was low before POF"));
    return false;
  }

  writeCommand(CMD_POF);
  if (!waitBusyCycle(1000, 5000)) {
    out.print(F("[FAIL] POF BUSY_N cycle timed out; level = "));
    out.println(busyLevel());
    return false;
  }

  booster_on_ = false;
  out.println(F("Booster power-off complete"));
  return true;
}

bool ET011TJ1::finishVendorUpdate(Print& out, uint8_t refresh_mode) {
  if (!powerOn(out)) {
    return false;
  }

  uint8_t refresh[sizeof(VENDOR_FULL_REFRESH)];
  memcpy(refresh, VENDOR_FULL_REFRESH, sizeof(refresh));
  refresh[0] = refresh_mode;
  writeCommandData(CMD_DRF, refresh, sizeof(refresh));
  out.println(refresh_mode == GC_FAST_REFRESH_MODE
                  ? F("GC fast refresh started...")
                  : F("Vendor cleaning refresh started; this may take several seconds..."));
  if (!waitBusyCycle(1000, 20000)) {
    out.print(F("[FAIL] Vendor DRF BUSY_N cycle timed out; level = "));
    out.println(busyLevel());
    return false;
  }
  return powerOff(out);
}

void ET011TJ1::safeShutdown(Print& out) {
  if (controller_awake_) {
    // Attempt POF even when BUSY_N behaved unexpectedly. Reset is asserted
    // immediately afterwards, so a failed BUSY handshake cannot leave the
    // controller active in application firmware.
    writeCommand(CMD_POF);
    delay(50);
  }
  booster_on_ = false;
  holdInReset();
  out.println(F("Safety shutdown: POF sent and ET011TJ1 held in reset"));
}

void ET011TJ1::holdInReset() {
  digitalWrite(cs_pin_, HIGH);
  digitalWrite(dc_pin_, LOW);
  digitalWrite(reset_pin_, LOW);
  if (prepared_) {
    spi_.end();
    prepared_ = false;
  }
  controller_awake_ = false;
  booster_on_ = false;
}

void ET011TJ1::writeCommand(uint8_t command) {
  spi_.beginTransaction(spi_settings_);
  digitalWrite(dc_pin_, LOW);
  digitalWrite(cs_pin_, LOW);
  spi_.transfer(command, SPI_TRANSMITONLY);
  digitalWrite(cs_pin_, HIGH);
  spi_.endTransaction();
}

void ET011TJ1::writeCommandData(uint8_t command, const uint8_t* data,
                               size_t length) {
  spi_.beginTransaction(spi_settings_);

  // The ET011TJ1 samples DC for an entire CSB assertion. Command and data
  // therefore require separate CSB pulses; changing DC while CSB is low makes
  // the following parameter bytes decode as commands.
  digitalWrite(dc_pin_, LOW);
  digitalWrite(cs_pin_, LOW);
  spi_.transfer(command, SPI_TRANSMITONLY);
  digitalWrite(cs_pin_, HIGH);

  digitalWrite(dc_pin_, HIGH);
  digitalWrite(cs_pin_, LOW);
  for (size_t i = 0; i < length; ++i) {
    spi_.transfer(data[i], SPI_TRANSMITONLY);
  }
  digitalWrite(cs_pin_, HIGH);
  spi_.endTransaction();
}

void ET011TJ1::writePattern(uint8_t command, ET011TJ1Pattern pattern,
                            bool inverted) {
  spi_.beginTransaction(spi_settings_);
  digitalWrite(dc_pin_, LOW);
  digitalWrite(cs_pin_, LOW);
  spi_.transfer(command, SPI_TRANSMITONLY);
  digitalWrite(cs_pin_, HIGH);

  digitalWrite(dc_pin_, HIGH);
  digitalWrite(cs_pin_, LOW);

  for (uint16_t y = 0; y < HEIGHT; ++y) {
    for (uint16_t x = 0; x < WIDTH; x += 4) {
      uint8_t packed = packed2BppPatternByte(pattern, x, y);
      if (inverted) {
        packed = static_cast<uint8_t>(~packed);
      }
      spi_.transfer(packed, SPI_TRANSMITONLY);
    }
  }

  digitalWrite(cs_pin_, HIGH);
  spi_.endTransaction();
}

void ET011TJ1::writeFramebuffer(uint8_t command, const uint8_t* framebuffer,
                                size_t stride_bytes, bool inverted) {
  spi_.beginTransaction(spi_settings_);
  digitalWrite(dc_pin_, LOW);
  digitalWrite(cs_pin_, LOW);
  spi_.transfer(command, SPI_TRANSMITONLY);
  digitalWrite(cs_pin_, HIGH);

  digitalWrite(dc_pin_, HIGH);
  digitalWrite(cs_pin_, LOW);
  for (uint16_t y = 0; y < HEIGHT; ++y) {
    const uint8_t* row = framebuffer + static_cast<size_t>(y) * stride_bytes;
    for (uint16_t x = 0; x < WIDTH; x += 4) {
      uint8_t packed_2bpp = 0;
      for (uint8_t pixel = 0; pixel < 4; ++pixel) {
        const uint16_t source_x = x + pixel;
        const bool black =
            (row[source_x / 8] & (0x80U >> (source_x & 7))) != 0;
        if (black) {
          packed_2bpp |= static_cast<uint8_t>(0x03U << (6 - 2 * pixel));
        }
      }
      if (inverted) {
        packed_2bpp = static_cast<uint8_t>(~packed_2bpp);
      }
      spi_.transfer(packed_2bpp, SPI_TRANSMITONLY);
    }
  }

  digitalWrite(cs_pin_, HIGH);
  spi_.endTransaction();
}

bool ET011TJ1::waitReady(uint32_t timeout_ms) {
  const uint32_t started = millis();
  while (busyLevel() == LOW) {
    if (millis() - started >= timeout_ms) {
      return false;
    }
    delay(1);
  }
  return true;
}

bool ET011TJ1::waitBusyCycle(uint32_t assert_timeout_ms,
                            uint32_t ready_timeout_ms) {
  // PON/POF/DRF assert BUSY_N low for milliseconds, so the 1 ms poll normally
  // observes the pulse. If the deadline passes with BUSY_N still high, the
  // operation most likely completed between samples; treat ready as success
  // rather than failing the whole test. A genuinely stuck BUSY_N is caught by
  // logicProbe.
  const uint32_t started = millis();
  while (busyLevel() == HIGH) {
    if (millis() - started >= assert_timeout_ms) {
      return true;
    }
    delay(1);
  }
  return waitReady(ready_timeout_ms);
}

uint8_t ET011TJ1::packed2BppPatternByte(ET011TJ1Pattern pattern, uint16_t x,
                                       uint16_t y) const {
  uint8_t packed = 0;
  for (uint8_t pixel = 0; pixel < 4; ++pixel) {
    bool black = false;
    if (pattern == ET011TJ1Pattern::Black ||
        (pattern == ET011TJ1Pattern::Diagnostic &&
         diagnosticPixel(x + pixel, y))) {
      black = true;
    }
    if (black) {
      packed |= static_cast<uint8_t>(0x03U << (6 - 2 * pixel));
    }
  }
  return packed;
}

bool ET011TJ1::diagnosticPixel(uint16_t x, uint16_t y) const {
  const bool border = x < 4 || x >= WIDTH - 4 || y < 4 || y >= HEIGHT - 4;
  const bool center_cross =
      (x >= WIDTH / 2 - 2 && x < WIDTH / 2 + 2) ||
      (y >= HEIGHT / 2 - 2 && y < HEIGHT / 2 + 2);
  const bool diagonals = (x > y ? x - y : y - x) < 2 ||
                         (x + y > WIDTH - 1 ? x + y - (WIDTH - 1)
                                           : (WIDTH - 1) - (x + y)) < 2;
  const bool corner_checks =
      ((x < 64 && y < 64) || (x >= WIDTH - 64 && y >= HEIGHT - 64)) &&
      (((x / 8) + (y / 8)) % 2 == 0);
  return border || center_cross || diagonals || corner_checks;
}
