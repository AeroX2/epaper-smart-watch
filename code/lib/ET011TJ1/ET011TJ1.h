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

  // Turns off the booster if necessary and holds the controller in reset.
  // Intended for firmware hand-off and recovery paths.
  void shutdown();

  // Resets the controller and verifies that active-low BUSY_N returns high.
  // This deliberately leaves the high-voltage booster off and returns the
  // controller to reset before returning.
  bool logicProbe(Print& out);

  // Performs one full refresh using the supplied ET011TJ2/ET011TT6 vendor
  // register and waveform sequence, then returns the controller to reset.
  bool show(ET011TJ1Pattern pattern, Print& out);

  // Streams a 240x240 one-bit framebuffer, where a set bit is black. The
  // framebuffer is converted to the controller's 2-bpp DTM format while it is
  // transmitted, so no second full-frame allocation is needed.
  bool showFrame(const uint8_t* framebuffer, size_t stride_bytes, Print& out);

  // Performs a GC fast update using the previous image as DTM1 and the new
  // image as DTM2. Epson's DN_EN refresh mode advances the panel history after
  // the update; callers must retain/copy the new framebuffer on success.
  bool showFrameFast(const uint8_t* framebuffer,
                     const uint8_t* previous_framebuffer,
                     size_t stride_bytes, Print& out);

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

  int hardwareReset();
  bool initializeVendorUpdate(Print& out,
                              const uint8_t* history_framebuffer = nullptr,
                              size_t history_stride_bytes = 0);
  bool configureVendorReference(Print& out);
  bool powerOn(Print& out);
  bool powerOff(Print& out);
  bool finishVendorUpdate(Print& out, uint8_t refresh_mode);
  void safeShutdown(Print& out);
  void holdInReset();

  void writeCommand(uint8_t command);
  void writeCommandData(uint8_t command, const uint8_t* data, size_t length);
  void writePattern(uint8_t command, ET011TJ1Pattern pattern,
                    bool inverted = false);
  void writeFramebuffer(uint8_t command, const uint8_t* framebuffer,
                        size_t stride_bytes, bool inverted = false);

  bool waitReady(uint32_t timeout_ms);
  bool waitBusyCycle(uint32_t assert_timeout_ms, uint32_t ready_timeout_ms);
  uint8_t packed2BppPatternByte(ET011TJ1Pattern pattern, uint16_t x,
                               uint16_t y) const;
  bool diagnosticPixel(uint16_t x, uint16_t y) const;
};
