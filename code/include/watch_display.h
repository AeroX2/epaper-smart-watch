#pragma once

#include <Arduino.h>

#include <ET011TJ1.h>

#include "watch_buttons.h"
#include "watch_ui.h"

class WatchDisplayController {
 public:
  WatchDisplayController(ET011TJ1& display, WatchUi& ui, WatchUiModel& model,
                         WatchButtons& buttons);

  void begin(Print& out);
  bool render(Print& out);
  bool renderFull(Print& out);
  bool active() const;
  // Ordinary renders use the quick 1-bpp update (ET011TJ1::showFrameKw); off
  // falls back to the slower GC fast update for comparison.
  void setKwMode(bool on) { kw_mode_ = on; }
  bool kwMode() const { return kw_mode_; }
  bool clockMinuteChanged() const;

 private:
  // A full cleaning refresh after this many quick ones; 0 = never (trial:
  // quick refreshes everywhere; console U still forces one).
  static constexpr uint8_t CLEAN_REFRESH_INTERVAL = 0;

  ET011TJ1& display_;
  WatchUi& ui_;
  WatchUiModel& model_;
  WatchButtons& buttons_;
  bool active_ = false;
  uint8_t updates_since_clean_refresh_ = 0;
  uint8_t last_rendered_minute_ = 0xFF;
  uint8_t previous_framebuffer_[MonochromeCanvas::BYTES]{};
  bool history_valid_ = false;
  bool kw_mode_ = true;

  bool renderImpl(Print& out, bool force_clean_refresh);
};
