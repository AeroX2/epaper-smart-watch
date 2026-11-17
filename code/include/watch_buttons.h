#pragma once

#include <Arduino.h>

// One debounced press of one button, reported when it is released. There are
// no long presses and no two-button gestures.
struct WatchButtonEvent {
  uint8_t button_index = 0;
};

class WatchButtons {
 public:
  void begin();
  bool poll(WatchButtonEvent& event);
  bool needsService() const;

  // SPI1 temporarily borrows button 4 as its unused MISO pin. Restore that pin
  // and resynchronise its debounce state after each display transaction.
  void restoreSharedSpiButton();

 private:
  static constexpr uint8_t BUTTON_COUNT = 4;
  static constexpr uint32_t DEBOUNCE_MS = 35;

  bool popPendingButton(WatchButtonEvent& event);

  bool raw_state_[BUTTON_COUNT]{};
  bool stable_state_[BUTTON_COUNT]{};
  uint32_t changed_at_[BUTTON_COUNT]{};
  uint8_t pending_release_mask_ = 0;
};
