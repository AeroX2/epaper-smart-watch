#include "watch_buttons.h"

#include "board_pins.h"

namespace {

// Index order is ButtonCorner; roles_ maps each corner to its role.
constexpr uint32_t BUTTON_PINS[] = {
    BoardPins::BUTTON_4,  // top-left, SW5
    BoardPins::BUTTON_3,  // top-right, SW4
    BoardPins::BUTTON_1,  // bottom-left, SW2 (also the DFU button at reset)
    BoardPins::BUTTON_2,  // bottom-right, SW3
};

// BUTTON_4 shares its pin with the display SPI bus.
constexpr uint8_t SHARED_SPI_INDEX =
    static_cast<uint8_t>(ButtonCorner::TopLeft);
static_assert(BUTTON_PINS[SHARED_SPI_INDEX] == BoardPins::BUTTON_4,
              "SHARED_SPI_INDEX must point at BUTTON_4");

}  // namespace

const uint8_t WatchButtons::DEFAULT_LAYOUT[BUTTON_COUNT] = {0, 2, 3, 1};

bool WatchButtons::validLayout(const uint8_t roles[BUTTON_COUNT]) {
  uint8_t seen = 0;
  for (uint8_t corner = 0; corner < BUTTON_COUNT; ++corner) {
    if (roles[corner] >= BUTTON_COUNT) {
      return false;
    }
    seen |= static_cast<uint8_t>(1U << roles[corner]);
  }
  return seen == 0x0F;
}

bool WatchButtons::setLayout(const uint8_t roles[BUTTON_COUNT]) {
  if (!validLayout(roles)) {
    return false;
  }
  memcpy(roles_, roles, BUTTON_COUNT);
  return true;
}

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
  pinMode(BUTTON_PINS[SHARED_SPI_INDEX], INPUT_PULLUP);
}

void WatchButtons::sample() {
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
    if (!pressed && queue_count_ < QUEUE_CAPACITY) {
      WatchButtonEvent& event =
          queue_[(queue_head_ + queue_count_) % QUEUE_CAPACITY];
      event.button_index = roles_[index];
      event.at_ms = now;
      ++queue_count_;
    }
  }
}

bool WatchButtons::poll(WatchButtonEvent& event) {
  sample();
  if (queue_count_ == 0) {
    event = {};
    return false;
  }
  event = queue_[queue_head_];
  queue_head_ = static_cast<uint8_t>((queue_head_ + 1) % QUEUE_CAPACITY);
  --queue_count_;
  return true;
}

bool WatchButtons::needsService() const {
  if (queue_count_ != 0) {
    return true;
  }
  for (uint8_t index = 0; index < BUTTON_COUNT; ++index) {
    if (raw_state_[index] || stable_state_[index]) {
      return true;
    }
  }
  return false;
}
