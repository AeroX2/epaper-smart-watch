#pragma once

#include <Arduino.h>

class WatchRtc;

enum WatchWakeEvent : uint8_t {
  WATCH_WAKE_NONE = 0,
  WATCH_WAKE_BUTTON = 1U << 0,
  WATCH_WAKE_ACCELEROMETER = 1U << 1,
  WATCH_WAKE_BATTERY = 1U << 2,
  WATCH_WAKE_USB_POWER = 1U << 3,  // VDDUSB appeared or disappeared (PVM1)
};

// Persisted deep-sleep policy (console commands 0 / 1 / 2).
enum class DeepSleepMode : uint8_t {
  Off = 0,           // never use Stop mode
  RadioOffOnly = 1,  // Stop 2 only while Bluetooth is off (default)
  Always = 2,        // also with the radio advertising/connected (opt-in)
};

// Why Stop mode is not being used right now. Any set bit means the loop
// falls back to ordinary Sleep mode (LowPower.idle()).
enum WatchSleepBlock : uint16_t {
  SLEEP_BLOCK_SETTING = 1U << 0,         // deep sleep switched off
  SLEEP_BLOCK_WATCHDOG_RESET = 1U << 1,  // this boot followed an IWDG reset
  SLEEP_BLOCK_STOP_FAULT = 1U << 2,      // last reset hit inside Stop, or a
                                         // wake did not restore the clock
  SLEEP_BLOCK_NO_BACKSTOP = 1U << 3,     // IWDG would not run in Stop / no RTC
  SLEEP_BLOCK_USB = 1U << 4,             // VDDUSB present (or not measurable)
  SLEEP_BLOCK_BOOT_GRACE = 1U << 5,      // first minute after boot
  SLEEP_BLOCK_ACTIVITY = 1U << 6,        // recent button or console input
  SLEEP_BLOCK_RADIO = 1U << 7,           // radio on and mode is not Always
  SLEEP_BLOCK_BUSY = 1U << 8,            // application work in progress
  SLEEP_BLOCK_DEBUGGER = 1U << 9,        // SWD debugger attached
};

// Application work that must keep the full clock tree (SLEEP_BLOCK_BUSY).
enum WatchSleepBusy : uint8_t {
  SLEEP_BUSY_NONE = 0,
  SLEEP_BUSY_ACTUATOR = 1U << 0,  // vibration pulse, tone or alert pattern
  SLEEP_BUSY_BUTTONS = 1U << 1,   // debounce / hold in progress
  SLEEP_BUSY_FLASH = 1U << 2,     // external flash held awake
  SLEEP_BUSY_OTA = 1U << 3,       // firmware update prompt or transfer
  SLEEP_BUSY_LINK = 1U << 4,      // phone link has data in flight
  SLEEP_BUSY_SERIAL = 1U << 5,    // console line being typed
};

// MCU sleep policy.
//
// sleep() picks between two modes every time the main loop has nothing to do:
//
//  * Stop 2 ("deep sleep"): all high-speed clocks off, tens of microamps.
//    Entered only when blockers() is zero. Woken by the RTC alarm (always
//    armed first, at most STOP_MAX_MS ahead so the 20 s IWDG is fed), the
//    button / BMA400 / MAX17048 EXTI lines, VDDUSB appearing (PVM1) and, when
//    the radio is on, the IPCC interrupt from CPU2.
//  * Sleep (LowPower.idle(), the behaviour before deep sleep existed): CPU
//    halted, clocks and regulator on, so USB CDC, timers and tones keep
//    working. At most IDLE_MAX_MS at a time.
//
// millis() is corrected from the RTC after either mode.
class WatchPower {
 public:
  // The IWDG times out after 20 s and keeps counting in Stop: wake well
  // inside that.
  static constexpr uint32_t STOP_MAX_MS = 10000;
  // Below this a Stop entry/exit (clock tree restart) is not worth it.
  static constexpr uint32_t STOP_MIN_MS = 30;
  static constexpr uint32_t IDLE_MAX_MS = 1000;
  // No Stop for this long after boot: a window in which USB can enumerate
  // and the console works whatever the VDDUSB detection says.
  static constexpr uint32_t BOOT_GRACE_MS = 60000;
  static constexpr uint32_t BUTTON_HOLD_MS = 30000;
  static constexpr uint32_t CONSOLE_HOLD_MS = 300000;

  void begin(WatchRtc& rtc, bool reset_by_watchdog, Print& out);
  uint8_t takeEvents();

  void setMode(DeepSleepMode mode) { mode_ = mode; }
  DeepSleepMode mode() const { return mode_; }

  // Keeps the watch out of Stop for hold_ms from now (button or console
  // activity). Only ever extends the current hold.
  void noteActivity(uint32_t hold_ms);

  // Called with interrupts disabled immediately before the core stops. If it
  // returns true the sleep is abandoned (work arrived after the loop's last
  // look). Must not enable interrupts or block.
  void setWakeVeto(bool (*veto)()) { veto_ = veto; }

  // Sleeps until an interrupt or for at most maximum_ms. radio_on and busy
  // (WatchSleepBusy bits) describe the application state for the policy.
  void sleep(uint32_t maximum_ms, bool radio_on, uint8_t busy);

  // WatchSleepBlock bits; zero means Stop mode would be used.
  uint16_t blockers(bool radio_on, uint8_t busy) const;
  // True while the VDDUSB monitor says the USB LDO is powered.
  bool usbPowered() const;

  // Selects the LSE as the radio's wake-up clock (ST requirement for the
  // BLE stack; the generic WB5MMG clock setup does not do it). Call before
  // the radio is started.
  static void selectRadioWakeClock();

  void printStatus(Print& out, bool radio_on, uint8_t busy);

  // Running totals since boot, for the power log (deltas between two
  // snapshots give one interval). Wake causes count Stop wakes only.
  static constexpr uint8_t WAKE_CAUSES = 7;  // RTC, button, accelerometer,
                                             // battery, radio, USB, other
  struct Totals {
    uint32_t stop_ms;
    uint32_t idle_ms;
    uint32_t stop_entries;
    uint32_t idle_entries;
    uint32_t wake_cause[WAKE_CAUSES];
  };
  void totals(Totals& out) const;
  // Every WatchSleepBlock / WatchSleepBusy bit seen since the last call.
  uint16_t takeBlocksSeen();
  uint8_t takeBusySeen();

 private:
  enum class StopResult : uint8_t { Slept, Abandoned, Unavailable };
  enum WakeCause : uint8_t {
    CAUSE_RTC,
    CAUSE_BUTTON,
    CAUSE_ACCELEROMETER,
    CAUSE_BATTERY,
    CAUSE_RADIO,
    CAUSE_USB,
    CAUSE_OTHER,
    CAUSE_COUNT,
  };

  static void onButtonInterrupt();
 public:
  // Called from the button interrupt before the wake event is set.
  static void setButtonEdgeHook(void (*hook)()) { button_edge_hook_ = hook; }
 private:
  static void (*button_edge_hook_)();
  static void onAccelerometerInterrupt();
  static void onBatteryInterrupt();
  static void setEvent(uint8_t event);
  friend void watchPowerUsbSupplyInterrupt();

  bool armRtcWake(uint32_t ms, uint64_t& target_ms);
  StopResult enterStop(uint32_t ms);
  void rollWindow(uint32_t now);
  static void printBlockers(Print& out, uint16_t blocks, uint8_t busy);

  static WatchPower* instance_;
  volatile uint8_t pending_events_ = WATCH_WAKE_NONE;
  WatchRtc* rtc_ = nullptr;
  bool (*veto_)() = nullptr;

  DeepSleepMode mode_ = DeepSleepMode::RadioOffOnly;
  bool reset_by_watchdog_ = false;
  bool stop_fault_ = false;      // see SLEEP_BLOCK_STOP_FAULT
  bool backstop_ok_ = false;     // IWDG runs in Stop
  bool usb_monitor_ok_ = false;  // PVM1 enabled
  uint32_t boot_ms_ = 0;
  uint32_t hold_until_ms_ = 0;
  bool hold_active_ = false;

  // Diagnostics.
  uint32_t stop_entries_ = 0;     // WFI executed with SLEEPDEEP
  uint32_t stop_system_ = 0;      // ... and the device really reached Stop
  uint32_t stop_abandoned_ = 0;   // veto / late event / alarm too close
  uint32_t stop_unavailable_ = 0; // wake source or semaphore check failed
  uint32_t idle_entries_ = 0;
  uint32_t stop_ms_total_ = 0;
  uint32_t idle_ms_total_ = 0;
  uint32_t wake_cause_[CAUSE_COUNT]{};
  uint16_t last_blocks_ = 0;
  uint16_t blocks_seen_ = 0;
  uint8_t busy_seen_ = 0;
  volatile uint32_t usb_supply_changes_ = 0;  // PVM1 edges seen

  // Rolling one-minute window.
  uint32_t window_started_ms_ = 0;
  uint32_t window_wakes_ = 0;
  uint32_t window_stop_ms_ = 0;
  uint32_t window_idle_ms_ = 0;
  uint32_t last_window_ms_ = 0;  // 0 = no complete window yet
  uint32_t last_window_wakes_ = 0;
  uint32_t last_window_stop_ms_ = 0;
  uint32_t last_window_idle_ms_ = 0;
};
