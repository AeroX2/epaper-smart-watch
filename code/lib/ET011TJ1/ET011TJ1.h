#pragma once

#include <Arduino.h>
#include <SPI.h>

enum class ET011TJ1Pattern : uint8_t {
  White,
  Black,
  Diagnostic,
};

class ET011TJ1 {
 public:
  static constexpr uint16_t WIDTH = 240;
  static constexpr uint16_t HEIGHT = 240;
  static constexpr size_t FRAME_2BPP_BYTES =
      static_cast<size_t>(WIDTH) * HEIGHT / 4;

  ET011TJ1(SPIClass& spi, uint32_t cs_pin, uint32_t dc_pin,
           uint32_t reset_pin, uint32_t busy_pin);

  // Configures the host pins and leaves the controller held in reset.
  void prepare();

  // Resets the controller and verifies that active-low BUSY_N returns high.
  // This deliberately leaves the high-voltage booster off and returns the
  // controller to reset before returning.
  bool logicProbe(Print& out);

  // Performs one full refresh and returns the controller to booster-off
  // standby, preserving its registers and frame history.
  bool show(ET011TJ1Pattern pattern, Print& out);

  int busyLevel() const;

 private:
  static constexpr uint32_t SPI_HZ = 4000000;

  SPIClass& spi_;
  SPISettings spi_settings_;
  uint32_t cs_pin_;
  uint32_t dc_pin_;
  uint32_t reset_pin_;
  uint32_t busy_pin_;
  bool prepared_ = false;
  bool controller_awake_ = false;
  bool booster_on_ = false;
  bool initialized_ = false;
  ET011TJ1Pattern last_pattern_ = ET011TJ1Pattern::White;

  int hardwareReset();
  bool initialize(Print& out);
  bool configureController(Print& out);
  bool powerOn(Print& out);
  bool powerOff(Print& out);
  bool refresh(Print& out);
  void safeShutdown(Print& out);
  void holdInReset();

  void writeCommand(uint8_t command);
  void writeCommandData(uint8_t command, const uint8_t* data, size_t length);
  void writePattern(uint8_t command, ET011TJ1Pattern pattern);

  bool waitReady(uint32_t timeout_ms);
  bool waitBusyCycle(uint32_t assert_timeout_ms, uint32_t ready_timeout_ms);
  uint8_t packed2BppPatternByte(ET011TJ1Pattern pattern, uint16_t x,
                               uint16_t y) const;
  bool diagnosticPixel(uint16_t x, uint16_t y) const;
};
