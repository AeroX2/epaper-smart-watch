#include "watch_buttons.h"

#include "board_pins.h"

namespace {

constexpr uint32_t BUTTON_PINS[] = {
    BoardPins::BUTTON_1,  // bottom-left: Home
    BoardPins::BUTTON_2,  // top-left: Previous
    BoardPins::BUTTON_3,  // top-right: Next
    BoardPins::BUTTON_4,  // bottom-right: Select
};

}  // namespace

void WatchButtons::begin() {
  const uint32_t now = millis();
  for (uint8_t index = 0; index < BUTTON_COUNT; ++index) {
    pinMode(BUTTON_PINS[index], INPUT_PULLUP);
    raw_state_[index] = digitalRead(BUTTON_PINS[index]) == LOW;
    stable_state_[index] = raw_state_[index];
    changed_at_[index] = now;
    pressed_at_[index] = now;
  }
}

void WatchButtons::restoreSharedSpiButton() {
  constexpr uint8_t index = 3;
  pinMode(BUTTON_PINS[index], INPUT_PULLUP);
  raw_state_[index] = digitalRead(BUTTON_PINS[index]) == LOW;
  stable_state_[index] = raw_state_[index];
  changed_at_[index] = millis();
  pressed_at_[index] = changed_at_[index];
}

uint8_t WatchButtons::stableMask() const {
  uint8_t mask = 0;
  for (uint8_t index = 0; index < BUTTON_COUNT; ++index) {
    if (stable_state_[index]) {
      mask |= static_cast<uint8_t>(1U << index);
    }
  }
  return mask;
}

bool WatchButtons::popPendingButton(WatchButtonEvent& event) {
  for (uint8_t index = 0; index < BUTTON_COUNT; ++index) {
    const uint8_t bit = static_cast<uint8_t>(1U << index);
    if ((pending_release_mask_ & bit) == 0) {
      continue;
    }
    pending_release_mask_ &= static_cast<uint8_t>(~bit);
    event.type = WatchButtonEventType::Button;
    event.button_index = index;
    event.long_press = (pending_long_press_mask_ & bit) != 0;
    pending_long_press_mask_ &= static_cast<uint8_t>(~bit);
    return true;
  }
  return false;
}

bool WatchButtons::poll(WatchButtonEvent& event) {
  event = {};
  if (popPendingButton(event)) {
    return true;
  }

  const uint32_t now = millis();
  uint8_t released_mask = 0;
  uint8_t released_long_mask = 0;
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
    if (pressed) {
      pressed_at_[index] = now;
    } else {
      const uint8_t bit = static_cast<uint8_t>(1U << index);
      released_mask |= bit;
      if (now - pressed_at_[index] >= LONG_PRESS_MS) {
        released_long_mask |= bit;
      }
    }
  }

  const uint8_t stable_mask = stableMask();
  if (active_chord_mask_ == 0) {
    if ((stable_mask & BACKSLASH_MASK) == BACKSLASH_MASK) {
      active_chord_mask_ = BACKSLASH_MASK;
    } else if ((stable_mask & SLASH_MASK) == SLASH_MASK) {
      active_chord_mask_ = SLASH_MASK;
    }
    if (active_chord_mask_ != 0) {
      chord_consumed_mask_ = active_chord_mask_;
      chord_started_at_ = now;
      chord_hold_reported_ = false;
    }
  }

  if (active_chord_mask_ != 0 && !chord_hold_reported_ &&
      now - chord_started_at_ >= CHORD_HOLD_MS) {
    chord_hold_reported_ = true;
    event.type = WatchButtonEventType::HoldArmed;
  }

  const uint8_t suppressed_releases = chord_consumed_mask_;
  if (active_chord_mask_ != 0 &&
      (stable_mask & active_chord_mask_) == 0) {
    const uint8_t completed_chord = active_chord_mask_;
    event.type = completed_chord == BACKSLASH_MASK
                     ? WatchButtonEventType::DiagonalBackslash
                     : WatchButtonEventType::DiagonalSlash;
    event.long_press = now - chord_started_at_ >= CHORD_HOLD_MS;
    active_chord_mask_ = 0;
    chord_consumed_mask_ = 0;
  }

  pending_release_mask_ |=
      static_cast<uint8_t>(released_mask & ~suppressed_releases);
  pending_long_press_mask_ |=
      static_cast<uint8_t>(released_long_mask & ~suppressed_releases);

  return event.type != WatchButtonEventType::None || popPendingButton(event);
}

bool WatchButtons::needsService() const {
  if (active_chord_mask_ != 0 || pending_release_mask_ != 0 ||
      pending_long_press_mask_ != 0) {
    return true;
  }
  for (uint8_t index = 0; index < BUTTON_COUNT; ++index) {
    if (raw_state_[index] || stable_state_[index]) {
      return true;
    }
  }
  return false;
}
