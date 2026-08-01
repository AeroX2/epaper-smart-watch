#include "watch_power.h"

#include <STM32LowPower.h>

#include "board_pins.h"
#include "watch_rtc.h"

extern __IO uint32_t uwTick;

WatchPower* WatchPower::instance_ = nullptr;

void WatchPower::begin(WatchRtc& rtc, Print& out) {
  rtc_ = &rtc;
  instance_ = this;
  LowPower.begin();

  LowPower.attachInterruptWakeup(BoardPins::BUTTON_1, onButtonInterrupt,
                                 CHANGE, SLEEP_MODE);
  LowPower.attachInterruptWakeup(BoardPins::BUTTON_2, onButtonInterrupt,
                                 CHANGE, SLEEP_MODE);
  LowPower.attachInterruptWakeup(BoardPins::BUTTON_3, onButtonInterrupt,
                                 CHANGE, SLEEP_MODE);
  LowPower.attachInterruptWakeup(BoardPins::BUTTON_4, onButtonInterrupt,
                                 CHANGE, SLEEP_MODE);
  LowPower.attachInterruptWakeup(BoardPins::ACCEL_INT1,
                                 onAccelerometerInterrupt, RISING, SLEEP_MODE);
  LowPower.attachInterruptWakeup(BoardPins::BATTERY_ALERT, onBatteryInterrupt,
                                 FALLING, SLEEP_MODE);
  out.println(F(
      "[PASS] Interrupt wake armed: buttons, BMA400, MAX17048, RTC/USB/radio."));
}

uint8_t WatchPower::takeEvents() {
  noInterrupts();
  const uint8_t events = pending_events_;
  pending_events_ = WATCH_WAKE_NONE;
  interrupts();
  return events;
}

void WatchPower::sleepFor(uint32_t maximum_ms) {
  if (rtc_ == nullptr || maximum_ms == 0) {
    return;
  }

  const uint64_t rtc_before = rtc_->milliseconds();
  const uint32_t tick_before = HAL_GetTick();
  // STM32LowPower::sleep() switches STM32WB to the 2 MHz MSI clock before
  // WFI. Native USB CDC can enumerate and then become unusable across that
  // clock transition. idle() still suspends SysTick and stops the M4 in
  // ordinary Sleep mode, but retains the main regulator and USB-safe clocks.
  LowPower.idle(maximum_ms);
  const uint32_t tick_after = HAL_GetTick();
  const uint64_t rtc_after = rtc_->milliseconds();

  const uint64_t rtc_elapsed_64 =
      rtc_after >= rtc_before ? rtc_after - rtc_before : 0;
  const uint32_t rtc_elapsed =
      rtc_elapsed_64 > UINT32_MAX ? UINT32_MAX
                                  : static_cast<uint32_t>(rtc_elapsed_64);
  const uint32_t active_tick_elapsed = tick_after - tick_before;
  if (rtc_elapsed > active_tick_elapsed) {
    uwTick += rtc_elapsed - active_tick_elapsed;
  }
}

void WatchPower::setEvent(uint8_t event) {
  if (instance_ != nullptr) {
    instance_->pending_events_ |= event;
  }
}

void WatchPower::onButtonInterrupt() {
  setEvent(WATCH_WAKE_BUTTON);
}

void WatchPower::onAccelerometerInterrupt() {
  setEvent(WATCH_WAKE_ACCELEROMETER);
}

void WatchPower::onBatteryInterrupt() {
  setEvent(WATCH_WAKE_BATTERY);
}
