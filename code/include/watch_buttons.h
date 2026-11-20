#pragma once

#include <Arduino.h>

// One press of one button, reported when it goes down. There are no long
// presses and no two-button gestures. button_index is the button's role, not
// its position: 0 Home/back, 1 Next, 2 Previous, 3 Select.
struct WatchButtonEvent {
  uint8_t button_index = 0;
  uint32_t at_ms = 0;  // millis() when the press was seen
};

// Physical buttons, in the order the phone's BUTTONS message lists them
// (seen from the display side: SW5, SW4, SW2, SW3 on the board).
enum class ButtonCorner : uint8_t { TopLeft, TopRight, BottomLeft, BottomRight };

class WatchButtons {
 public:
  // Presses are counted in the pin interrupt (onEdge, called from the
  // shared button EXTI handler), so none is missed however busy the main
  // loop is: each edge is judged the moment it happens, and edges within
  // DEBOUNCE_MS of the last accepted one on that button are contact bounce.
  void begin();
  static void onEdge();
  bool poll(WatchButtonEvent& event);
  // Catches up with a release the interrupt judged as bounce. Cheap; call
  // from the main loop and the display wait hook.
  void sample();
  bool needsService() const;

  // SPI1 names button 4's pin as its unused MISO. While the display sets SPI
  // up that pin is not a button (held = true); the driver hands it back right
  // after. restoreSharedSpiButton() makes sure it is an input after display
  // work.
  void setSharedHeld(bool held);
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

  void edge();

  static WatchButtons* instance_;
  // Written in the interrupt; read with interrupts off.
  volatile bool down_[BUTTON_COUNT]{};
  volatile uint32_t edge_ms_[BUTTON_COUNT]{};
  volatile bool shared_held_ = false;
  volatile uint32_t shared_ignore_until_ms_ = 0;
  WatchButtonEvent queue_[QUEUE_CAPACITY]{};
  volatile uint8_t queue_head_ = 0;
  volatile uint8_t queue_count_ = 0;
  uint8_t roles_[BUTTON_COUNT] = {0, 2, 3, 1};
};
