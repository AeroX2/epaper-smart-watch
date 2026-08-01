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
  bool clockMinuteChanged() const;

 private:
  static constexpr uint8_t CLEAN_REFRESH_INTERVAL = 10;

  ET011TJ1& display_;
  WatchUi& ui_;
  WatchUiModel& model_;
  WatchButtons& buttons_;
  bool active_ = false;
  uint8_t updates_since_clean_refresh_ = 0;
  uint8_t last_rendered_minute_ = 0xFF;
  uint8_t previous_framebuffer_[MonochromeCanvas::BYTES]{};
  bool history_valid_ = false;

  bool renderImpl(Print& out, bool force_clean_refresh);
};
