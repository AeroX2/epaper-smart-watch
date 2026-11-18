#include "watch_buttons.h"

#include "board_pins.h"

namespace {

// Index order is the logical role: 0 Home/back, 1 Next, 2 Previous,
// 3 Select.
constexpr uint32_t BUTTON_PINS[] = {
    BoardPins::BUTTON_4,  // bottom-right: Home
    BoardPins::BUTTON_2,  // top-left: Next
    BoardPins::BUTTON_3,  // top-right: Previous
    BoardPins::BUTTON_1,  // bottom-left: Select
};

// BUTTON_4 shares its pin with the display SPI bus.
constexpr uint8_t SHARED_SPI_INDEX = 0;
static_assert(BUTTON_PINS[SHARED_SPI_INDEX] == BoardPins::BUTTON_4,
              "SHARED_SPI_INDEX must point at BUTTON_4");

}  // namespace

void WatchButtons::begin() {
  const uint32_t now = millis();
  for (uint8_t index = 0; index < BUTTON_COUNT; ++index) {
    pinMode(BUTTON_PINS[index], INPUT_PULLUP);
    raw_state_[index] = digitalRead(BUTTON_PINS[index]) == LOW;
    stable_state_[index] = raw_state_[index];
    changed_at_[index] = now;
  }
}

void WatchButtons::restoreSharedSpiButton() {
  constexpr uint8_t index = SHARED_SPI_INDEX;
  pinMode(BUTTON_PINS[index], INPUT_PULLUP);
  raw_state_[index] = digitalRead(BUTTON_PINS[index]) == LOW;
  stable_state_[index] = raw_state_[index];
  changed_at_[index] = millis();
}

bool WatchButtons::popPendingButton(WatchButtonEvent& event) {
  for (uint8_t index = 0; index < BUTTON_COUNT; ++index) {
    const uint8_t bit = static_cast<uint8_t>(1U << index);
    if ((pending_release_mask_ & bit) == 0) {
      continue;
    }
    pending_release_mask_ &= static_cast<uint8_t>(~bit);
    event.button_index = index;
    return true;
  }
  return false;
}

bool WatchButtons::poll(WatchButtonEvent& event) {
  event = {};
  const uint32_t now = millis();
  for (uint8_t index = 0; index < BUTTON_COUNT; ++index) {
    const bool pressed = digitalRead(BUTTON_PINS[index]) == LOW;
    if (pressed != raw_state_[index]) {
      raw_state_[index] = pressed;
      changed_at_[index] = now;
    }
    if (pressed == stable_state_[index] ||
        now - changed_at_[index] < DEBOUNCE_MS) {
      continue;
    }
    stable_state_[index] = pressed;
    if (!pressed) {
      pending_release_mask_ |= static_cast<uint8_t>(1U << index);
    }
  }
  return popPendingButton(event);
}

bool WatchButtons::needsService() const {
  if (pending_release_mask_ != 0) {
    return true;
  }
  for (uint8_t index = 0; index < BUTTON_COUNT; ++index) {
    if (raw_state_[index] || stable_state_[index]) {
      return true;
    }
  }
  return false;
}
