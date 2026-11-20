#pragma once

#include <Arduino.h>

// One debounced press of one button, reported when it is released. There are
// no long presses and no two-button gestures. button_index is the button's
// role, not its position: 0 Home/back, 1 Next, 2 Previous, 3 Select.
struct WatchButtonEvent {
  uint8_t button_index = 0;
  uint32_t at_ms = 0;  // millis() when the release was seen
};

// Physical buttons, in the order the phone's BUTTONS message lists them
// (seen from the display side: SW5, SW4, SW2, SW3 on the board).
enum class ButtonCorner : uint8_t { TopLeft, TopRight, BottomLeft, BottomRight };

class WatchButtons {
 public:
  void begin();
  bool poll(WatchButtonEvent& event);
  // Debounces and queues releases without handing any out. Safe from the
  // display wait hook.
  void sample();
  bool needsService() const;

  // SPI1 names button 4's pin as its unused MISO. The display driver hands
  // it back right after setting SPI up; this makes sure it is an input again
  // after display work, without disturbing a press in progress.
  void restoreSharedSpiButton();

  static constexpr uint8_t BUTTON_COUNT = 4;
  // roles[corner] = role of that corner (ButtonCorner order). Must use each
  // role 0-3 exactly once; anything else is refused and changes nothing.
  static bool validLayout(const uint8_t roles[BUTTON_COUNT]);
  bool setLayout(const uint8_t roles[BUTTON_COUNT]);
  const uint8_t* layout() const { return roles_; }
  // Top-left Home, top-right Previous, bottom-left Select, bottom-right Next.
  static const uint8_t DEFAULT_LAYOUT[BUTTON_COUNT];

 private:
  static constexpr uint32_t DEBOUNCE_MS = 35;

  static constexpr uint8_t QUEUE_CAPACITY = 8;

  bool raw_state_[BUTTON_COUNT]{};
  bool stable_state_[BUTTON_COUNT]{};
  uint32_t changed_at_[BUTTON_COUNT]{};
  WatchButtonEvent queue_[QUEUE_CAPACITY]{};
  uint8_t queue_head_ = 0;
  uint8_t queue_count_ = 0;
  uint8_t roles_[BUTTON_COUNT] = {0, 2, 3, 1};
};
