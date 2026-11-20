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

WatchButtons* WatchButtons::instance_ = nullptr;

void WatchButtons::begin() {
  const uint32_t now = millis();
  for (uint8_t index = 0; index < BUTTON_COUNT; ++index) {
    pinMode(BUTTON_PINS[index], INPUT_PULLUP);
    down_[index] = digitalRead(BUTTON_PINS[index]) == LOW;
    edge_ms_[index] = now;
  }
  instance_ = this;
}

void WatchButtons::onEdge() {
  if (instance_ != nullptr) {
    instance_->edge();
  }
}

// Interrupt context. One handler serves all four pins, so look at each.
void WatchButtons::edge() {
  const uint32_t now = millis();
  for (uint8_t index = 0; index < BUTTON_COUNT; ++index) {
    if (index == SHARED_SPI_INDEX &&
        (shared_held_ ||
         static_cast<int32_t>(now - shared_ignore_until_ms_) < 0)) {
      continue;
    }
    const bool low = digitalRead(BUTTON_PINS[index]) == LOW;
    if (low == down_[index]) {
      continue;  // nothing new on this pin (or a held button)
    }
    if (now - edge_ms_[index] < DEBOUNCE_MS) {
      continue;  // contact bounce
    }
    down_[index] = low;
    edge_ms_[index] = now;
    if (low && queue_count_ < QUEUE_CAPACITY) {
      WatchButtonEvent& event =
          queue_[(queue_head_ + queue_count_) % QUEUE_CAPACITY];
      event.button_index = roles_[index];
      event.at_ms = now;
      queue_count_ = static_cast<uint8_t>(queue_count_ + 1);
    }
  }
}

void WatchButtons::sample() {
  // A release inside the bounce lockout is ignored by the interrupt; notice
  // it here so the button is ready for its next press.
  const uint32_t now = millis();
  for (uint8_t index = 0; index < BUTTON_COUNT; ++index) {
    if (index == SHARED_SPI_INDEX && shared_held_) {
      continue;
    }
    noInterrupts();
    if (down_[index] && digitalRead(BUTTON_PINS[index]) == HIGH &&
        now - edge_ms_[index] >= DEBOUNCE_MS) {
      down_[index] = false;
      edge_ms_[index] = now;
    }
    interrupts();
  }
}

bool WatchButtons::poll(WatchButtonEvent& event) {
  sample();
  noInterrupts();
  if (queue_count_ == 0) {
    interrupts();
    event = {};
    return false;
  }
  event = queue_[queue_head_];
  queue_head_ = static_cast<uint8_t>((queue_head_ + 1) % QUEUE_CAPACITY);
  queue_count_ = static_cast<uint8_t>(queue_count_ - 1);
  interrupts();
  return true;
}

bool WatchButtons::needsService() const {
  if (queue_count_ != 0) {
    return true;
  }
  for (uint8_t index = 0; index < BUTTON_COUNT; ++index) {
    if (down_[index]) {
      return true;
    }
  }
  return false;
}

void WatchButtons::setSharedHeld(bool held) {
  constexpr uint8_t index = SHARED_SPI_INDEX;
  if (held) {
    shared_held_ = true;
    return;
  }
  // The switch back to an input can itself raise an edge: ignore it.
  noInterrupts();
  shared_ignore_until_ms_ = millis() + 3;
  down_[index] = false;
  shared_held_ = false;
  interrupts();
}

void WatchButtons::restoreSharedSpiButton() {
  pinMode(BUTTON_PINS[SHARED_SPI_INDEX], INPUT_PULLUP);
}
