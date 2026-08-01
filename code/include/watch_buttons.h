#pragma once

#include <Arduino.h>

enum class WatchButtonEventType : uint8_t {
  None,
  Button,
  DiagonalBackslash,
  DiagonalSlash,
  HoldArmed,
};

struct WatchButtonEvent {
  WatchButtonEventType type = WatchButtonEventType::None;
  uint8_t button_index = 0;
  bool long_press = false;
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
  static constexpr uint32_t LONG_PRESS_MS = 800;
  static constexpr uint32_t CHORD_HOLD_MS = 650;
  static constexpr uint8_t BACKSLASH_MASK = (1U << 1) | (1U << 3);
  static constexpr uint8_t SLASH_MASK = (1U << 0) | (1U << 2);

  bool popPendingButton(WatchButtonEvent& event);
  uint8_t stableMask() const;

  bool raw_state_[BUTTON_COUNT]{};
  bool stable_state_[BUTTON_COUNT]{};
  uint32_t changed_at_[BUTTON_COUNT]{};
  uint32_t pressed_at_[BUTTON_COUNT]{};
  uint8_t active_chord_mask_ = 0;
  uint8_t chord_consumed_mask_ = 0;
  uint32_t chord_started_at_ = 0;
  bool chord_hold_reported_ = false;
  uint8_t pending_release_mask_ = 0;
  uint8_t pending_long_press_mask_ = 0;
};
