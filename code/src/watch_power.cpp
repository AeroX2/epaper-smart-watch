#include "watch_power.h"

#include <STM32LowPower.h>
#include <STM32RTC.h>

#include "board_pins.h"
#include "lock_resource.h"
#include "stm32yyxx_ll_exti.h"
#include "stm32yyxx_ll_hsem.h"
#include "stm32yyxx_ll_pwr.h"
#include "stm32yyxx_ll_rcc.h"
#include "watch_rtc.h"

extern __IO uint32_t uwTick;

extern "C" void SystemClock_Config(void);
extern "C" void configIPClock(void);

namespace {

// Set (in an RTC backup register, which survives every reset short of losing
// power) for exactly as long as the core is inside a Stop attempt, including
// the clock restart afterwards. Found set at boot, it means the previous
// reset happened in there. DR1/4/10 belong to the Arduino core, DR6/7 to
// STM32RTC, DR12/13 to WatchRtc and DR19 to the DFU request.
constexpr uint32_t STOP_MARKER_MAGIC = 0x570BD0E5UL;
#define WATCH_STOP_MARKER_REGISTER (RTC->BKP15R)

// The RTC alarm has a resolution of 1/256 s; never enter Stop for an alarm
// that is this close, it may already have been served.
constexpr uint32_t ALARM_MARGIN_MS = 12;

bool debuggerAttached() {
  return (CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) != 0;
}

// Alarm A enabled with its interrupt, routed through EXTI line 17 (the only
// path that can wake the device from Stop) and enabled in the NVIC.
bool rtcWakeArmed() {
  return (RTC->CR & RTC_CR_ALRAE) != 0 && (RTC->CR & RTC_CR_ALRAIE) != 0 &&
         LL_EXTI_IsEnabledIT_0_31(LL_EXTI_LINE_17) != 0 &&
         LL_EXTI_IsEnabledRisingTrig_0_31(LL_EXTI_LINE_17) != 0 &&
         NVIC_GetEnableIRQ(RTC_Alarm_IRQn) != 0;
}

bool lockRccSemaphore() {
  for (uint32_t tries = 0; tries < 0x10000UL; ++tries) {
    if (LL_HSEM_1StepLock(HSEM, CFG_HW_RCC_SEMID) == 0) {
      return true;
    }
  }
  return false;
}

// Flash latency stays at the 64 MHz setting, which is also valid at 16 MHz.
void switchSystemClockToHsi() {
  LL_RCC_HSI_Enable();
  for (uint32_t tries = 0; tries < 100000UL && !LL_RCC_HSI_IsReady();
       ++tries) {
  }
  if (!LL_RCC_HSI_IsReady()) {
    return;
  }
  LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_HSI);
  for (uint32_t tries = 0;
       tries < 100000UL &&
       LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_HSI;
       ++tries) {
  }
}

void printMs(Print& out, uint32_t ms) {
  out.print(ms / 1000UL);
  out.print('.');
  out.print((ms % 1000UL) / 100UL);
  out.print(F(" s"));
}

}  // namespace

WatchPower* WatchPower::instance_ = nullptr;

void watchPowerUsbSupplyInterrupt() {
  if (WatchPower::instance_ != nullptr) {
    ++WatchPower::instance_->usb_supply_changes_;
  }
  WatchPower::setEvent(WATCH_WAKE_USB_POWER);
}

// VDDUSB crossed the PVM1 threshold: USB was plugged in or removed. The
// handler only has to exist and acknowledge the line; waking the core is the
// point, the policy re-reads the monitor output on every sleep decision.
extern "C" void PVD_PVM_IRQHandler(void) {
  LL_EXTI_ClearFlag_0_31(LL_EXTI_LINE_16 | LL_EXTI_LINE_31);
  LL_EXTI_ClearFlag_32_63(LL_EXTI_LINE_33);
  watchPowerUsbSupplyInterrupt();
}

void WatchPower::begin(WatchRtc& rtc, bool reset_by_watchdog, Print& out) {
  rtc_ = &rtc;
  instance_ = this;
  reset_by_watchdog_ = reset_by_watchdog;
  boot_ms_ = millis();
  window_started_ms_ = boot_ms_;
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

  // DBGMCU.CR survives every reset except power-on, so a past debug session
  // can leave "keep the clocks running in Stop" set for good. Clear it unless
  // a debugger is attached right now (which blocks Stop anyway).
  if (!debuggerAttached()) {
    CLEAR_BIT(DBGMCU->CR, DBGMCU_CR_DBG_SLEEP | DBGMCU_CR_DBG_STOP |
                              DBGMCU_CR_DBG_STANDBY);
  }

  HAL_PWR_EnableBkUpAccess();
  if (WATCH_STOP_MARKER_REGISTER == STOP_MARKER_MAGIC) {
    stop_fault_ = true;
    WATCH_STOP_MARKER_REGISTER = 0;
  }

  // Without a watchdog that keeps counting in Stop, a wake that never comes
  // would need the battery desoldered. Never rely on anything else.
  backstop_ok_ = (FLASH->OPTR & FLASH_OPTR_IWDG_STOP) != 0;

  // CPU2 has its own low-power mode request and the device only goes as deep
  // as the shallower of the two. ST's sequence: allow the deepest mode before
  // CPU2 is started; once it runs it owns this register.
  if (!LL_PWR_IsEnabledBootC2()) {
    LL_C2_PWR_SetPowerMode(LL_PWR_MODE_SHUTDOWN);
  }
  // IPCC "CPU1 receive" must be able to wake CPU1 out of Stop.
  LL_EXTI_EnableIT_32_63(LL_EXTI_LINE_36);

  // VDDUSB monitor: its LDO is only powered from the USB connector, so this
  // is the "USB attached" input. Both edges interrupt so that plugging in
  // ends a Stop at once.
  PWR_PVMTypeDef monitor{};
  monitor.PVMType = PWR_PVM_1;
  monitor.Mode = PWR_PVM_MODE_IT_RISING_FALLING;
  const bool configured = HAL_PWREx_ConfigPVM(&monitor) == HAL_OK;
  HAL_PWREx_EnablePVM1();
  usb_monitor_ok_ = configured && (PWR->CR2 & PWR_CR2_PVME1) != 0;
  HAL_NVIC_SetPriority(PVD_PVM_IRQn, 14, 0);
  HAL_NVIC_EnableIRQ(PVD_PVM_IRQn);

  out.println(F(
      "[PASS] Interrupt wake armed: buttons, BMA400, MAX17048, RTC/USB/radio."));
  if (reset_by_watchdog_ || stop_fault_) {
    out.println(F(
        "[WARN] Deep sleep disabled for this boot (watchdog reset or a reset "
        "inside Stop mode)."));
  }
  if (!backstop_ok_) {
    out.println(F(
        "[WARN] Option bytes freeze the IWDG in Stop: deep sleep disabled."));
  }
}

uint8_t WatchPower::takeEvents() {
  noInterrupts();
  const uint8_t events = pending_events_;
  pending_events_ = WATCH_WAKE_NONE;
  interrupts();
  return events;
}

void WatchPower::noteActivity(uint32_t hold_ms) {
  const uint32_t until = millis() + hold_ms;
  if (!hold_active_ ||
      static_cast<int32_t>(until - hold_until_ms_) > 0) {
    hold_until_ms_ = until;
    hold_active_ = true;
  }
}

bool WatchPower::usbPowered() const {
  // PVMO1 is 1 while VDDUSB is below the 1.2 V threshold.
  return (PWR->SR2 & PWR_SR2_PVMO1) == 0;
}

uint16_t WatchPower::blockers(bool radio_on, uint8_t busy) const {
  uint16_t blocks = 0;
  if (mode_ == DeepSleepMode::Off) {
    blocks |= SLEEP_BLOCK_SETTING;
  }
  if (reset_by_watchdog_) {
    blocks |= SLEEP_BLOCK_WATCHDOG_RESET;
  }
  if (stop_fault_) {
    blocks |= SLEEP_BLOCK_STOP_FAULT;
  }
  if (!backstop_ok_ || rtc_ == nullptr) {
    blocks |= SLEEP_BLOCK_NO_BACKSTOP;
  }
  // An unusable monitor counts as "attached": never Stop on a guess.
  if (!usb_monitor_ok_ || usbPowered()) {
    blocks |= SLEEP_BLOCK_USB;
  }
  const uint32_t now = millis();
  if (now - boot_ms_ < BOOT_GRACE_MS) {
    blocks |= SLEEP_BLOCK_BOOT_GRACE;
  }
  if (hold_active_ && static_cast<int32_t>(hold_until_ms_ - now) > 0) {
    blocks |= SLEEP_BLOCK_ACTIVITY;
  }
  if (radio_on && mode_ != DeepSleepMode::Always) {
    blocks |= SLEEP_BLOCK_RADIO;
  }
  if (busy != SLEEP_BUSY_NONE) {
    blocks |= SLEEP_BLOCK_BUSY;
  }
  if (debuggerAttached()) {
    blocks |= SLEEP_BLOCK_DEBUGGER;
  }
  return blocks;
}

void WatchPower::selectRadioWakeClock() {
  if (!LL_RCC_LSE_IsReady() || !lockRccSemaphore()) {
    return;
  }
  LL_RCC_SetRFWKPClockSource(LL_RCC_RFWKP_CLKSOURCE_LSE);
  LL_HSEM_ReleaseLock(HSEM, CFG_HW_RCC_SEMID, 0);
}

bool WatchPower::armRtcWake(uint32_t ms, uint64_t& target_ms) {
  STM32RTC& rtc = STM32RTC::getInstance();
  if (!rtc.isConfigured()) {
    return false;
  }
  // The radio start-up leaves backup-domain write access switched off; the
  // alarm registers and the Stop marker both need it.
  HAL_PWR_EnableBkUpAccess();
  uint32_t milliseconds = 0;
  const time_t seconds = rtc.getEpoch(&milliseconds);
  target_ms = static_cast<uint64_t>(seconds) * 1000ULL + milliseconds + ms;
  rtc.setAlarmEpoch(static_cast<time_t>(target_ms / 1000ULL),
                    STM32RTC::MATCH_DHHMMSS,
                    static_cast<uint32_t>(target_ms % 1000ULL));
  return rtcWakeArmed();
}

// Stop 2 entry and exit, following the STM32WB dual-core sequence from ST's
// reference low-power interface (stm32_lpm_if.c, AN5289):
//
//  * the RCC semaphore (3) guards every clock change against CPU2;
//  * the "entry stop mode" semaphore (4) decides who moves the system clock
//    to HSI16 before Stop: CPU1 if CPU2 is already stopped, otherwise CPU1
//    keeps the semaphore and CPU2 does it when it stops later;
//  * HSI16 is the wake-up clock (set by the core's SystemClock_Config);
//  * on wake the semaphore is released and the normal clock tree is rebuilt
//    by the core's own SystemClock_Config(), which takes the RCC semaphore.
//
// Interrupts stay masked from the last "anything pending?" check until the
// clocks are back: an interrupt that becomes pending in that window makes WFI
// return immediately instead of being lost, and no handler runs on the
// reduced clock.
WatchPower::StopResult WatchPower::enterStop(uint32_t ms) {
  if (LL_RCC_GetClkAfterWakeFromStop() != LL_RCC_STOP_WAKEUPCLOCK_HSI) {
    return StopResult::Unavailable;
  }
  uint64_t target_ms = 0;
  if (!armRtcWake(ms, target_ms)) {
    return StopResult::Unavailable;
  }

  __disable_irq();
  if (pending_events_ != WATCH_WAKE_NONE || (veto_ != nullptr && veto_())) {
    __enable_irq();
    return StopResult::Abandoned;  // work arrived: back to the loop
  }
  // If the alarm has already been served nothing would end the Stop. Use
  // Sleep mode for what is left instead (returning to the loop here could
  // spin, since the loop would ask for the same sleep again).
  const uint64_t now_ms = rtc_->milliseconds();
  if (now_ms == 0 || now_ms + ALARM_MARGIN_MS >= target_ms ||
      !rtcWakeArmed()) {
    __enable_irq();
    return StopResult::Unavailable;
  }

  if (!lockRccSemaphore()) {
    __enable_irq();
    return StopResult::Unavailable;
  }
  bool to_hsi = true;
  if (LL_HSEM_1StepLock(HSEM, CFG_HW_ENTRY_STOP_MODE_SEMID) == 0) {
    if (LL_PWR_IsActiveFlag_C2DS() || LL_PWR_IsActiveFlag_C2SB()) {
      LL_HSEM_ReleaseLock(HSEM, CFG_HW_ENTRY_STOP_MODE_SEMID, 0);
    } else {
      to_hsi = false;  // CPU2 is awake: it switches when it stops
    }
  }
  if (to_hsi) {
    switchSystemClockToHsi();
  }
  LL_HSEM_ReleaseLock(HSEM, CFG_HW_RCC_SEMID, 0);

  HAL_SuspendTick();
  SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk;
  WATCH_STOP_MARKER_REGISTER = STOP_MARKER_MAGIC;
  LL_PWR_ClearFlag_C1STOP_C1STB();
  LL_PWR_SetPowerMode(LL_PWR_MODE_STOP2);
  SET_BIT(SCB->SCR, SCB_SCR_SLEEPDEEP_Msk);
  __DSB();
  __WFI();
  __ISB();
  CLEAR_BIT(SCB->SCR, SCB_SCR_SLEEPDEEP_Msk);

  const bool system_stopped = LL_PWR_IsActiveFlag_C1STOP() != 0;
  WakeCause cause = CAUSE_OTHER;
  if (NVIC_GetPendingIRQ(EXTI2_IRQn) || NVIC_GetPendingIRQ(EXTI4_IRQn) ||
      NVIC_GetPendingIRQ(EXTI15_10_IRQn)) {
    cause = CAUSE_BUTTON;
  } else if (NVIC_GetPendingIRQ(EXTI0_IRQn)) {
    cause = CAUSE_ACCELEROMETER;
  } else if (NVIC_GetPendingIRQ(EXTI3_IRQn)) {
    cause = CAUSE_BATTERY;
  } else if (NVIC_GetPendingIRQ(IPCC_C1_RX_IRQn) ||
             NVIC_GetPendingIRQ(IPCC_C1_TX_IRQn)) {
    cause = CAUSE_RADIO;
  } else if (NVIC_GetPendingIRQ(PVD_PVM_IRQn) ||
             NVIC_GetPendingIRQ(USB_LP_IRQn) ||
             NVIC_GetPendingIRQ(USB_HP_IRQn)) {
    cause = CAUSE_USB;
  } else if (NVIC_GetPendingIRQ(RTC_Alarm_IRQn)) {
    cause = CAUSE_RTC;
  }

  LL_HSEM_ReleaseLock(HSEM, CFG_HW_ENTRY_STOP_MODE_SEMID, 0);
  if (LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_PLL) {
    // Same call the STM32LowPower library makes after Stop. It spins (and so
    // ends in a watchdog reset, which then disables Stop) if an oscillator
    // never comes back.
    configIPClock();
    SystemClock_Config();
  }
  if (LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_PLL) {
    stop_fault_ = true;
  }
  WATCH_STOP_MARKER_REGISTER = 0;
  HAL_ResumeTick();
  __enable_irq();

  ++stop_entries_;
  if (system_stopped) {
    ++stop_system_;
  }
  ++wake_cause_[cause];
  return StopResult::Slept;
}

void WatchPower::totals(Totals& out) const {
  static_assert(static_cast<uint8_t>(CAUSE_COUNT) == WAKE_CAUSES,
                "WAKE_CAUSES must match WakeCause");
  out.stop_ms = stop_ms_total_;
  out.idle_ms = idle_ms_total_;
  out.stop_entries = stop_entries_;
  out.idle_entries = idle_entries_;
  for (uint8_t cause = 0; cause < WAKE_CAUSES; ++cause) {
    out.wake_cause[cause] = wake_cause_[cause];
  }
}

uint16_t WatchPower::takeBlocksSeen() {
  const uint16_t seen = blocks_seen_;
  blocks_seen_ = 0;
  return seen;
}

uint8_t WatchPower::takeBusySeen() {
  const uint8_t seen = busy_seen_;
  busy_seen_ = 0;
  return seen;
}

void WatchPower::rollWindow(uint32_t now) {
  const uint32_t elapsed = now - window_started_ms_;
  if (elapsed < 60000UL) {
    return;
  }
  last_window_ms_ = elapsed;
  last_window_wakes_ = window_wakes_;
  last_window_stop_ms_ = window_stop_ms_;
  last_window_idle_ms_ = window_idle_ms_;
  window_started_ms_ = now;
  window_wakes_ = 0;
  window_stop_ms_ = 0;
  window_idle_ms_ = 0;
}

void WatchPower::sleep(uint32_t maximum_ms, bool radio_on, uint8_t busy) {
  if (rtc_ == nullptr || maximum_ms == 0) {
    return;
  }
  // An event latched since the loop last collected them: service it first.
  if (pending_events_ != WATCH_WAKE_NONE) {
    return;
  }
  if (hold_active_ &&
      static_cast<int32_t>(hold_until_ms_ - millis()) <= 0) {
    hold_active_ = false;
  }

  const uint16_t blocks = blockers(radio_on, busy);
  last_blocks_ = blocks;
  blocks_seen_ |= blocks;
  busy_seen_ |= busy;

  const uint64_t rtc_before = rtc_->milliseconds();
  const uint32_t tick_before = HAL_GetTick();

  bool stopped = false;
  bool slept = false;
  if (blocks == 0 && maximum_ms >= STOP_MIN_MS) {
    switch (enterStop(maximum_ms < STOP_MAX_MS ? maximum_ms : STOP_MAX_MS)) {
      case StopResult::Slept:
        stopped = true;
        slept = true;
        break;
      case StopResult::Abandoned:
        ++stop_abandoned_;
        return;  // the loop has work, or the deadline is already here
      case StopResult::Unavailable:
        ++stop_unavailable_;
        break;
    }
  }
  if (!slept) {
    if (veto_ != nullptr && veto_()) {
      return;
    }
    // Ordinary Sleep mode at the full clock: SysTick is suspended, the main
    // regulator and every clock stay on, so USB CDC, PWM and tones keep
    // working. (STM32LowPower::sleep() would drop to the 2 MHz MSI, which
    // breaks native USB.)
    LowPower.idle(maximum_ms < IDLE_MAX_MS ? maximum_ms : IDLE_MAX_MS);
    ++idle_entries_;
  }

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

  ++window_wakes_;
  if (stopped) {
    stop_ms_total_ += rtc_elapsed;
    window_stop_ms_ += rtc_elapsed;
  } else {
    idle_ms_total_ += rtc_elapsed;
    window_idle_ms_ += rtc_elapsed;
  }
  rollWindow(millis());
}

void WatchPower::printBlockers(Print& out, uint16_t blocks, uint8_t busy) {
  struct Name {
    uint16_t bit;
    const char* text;
  };
  static const Name BLOCKS[] = {
      {SLEEP_BLOCK_SETTING, "switched off (console 0)"},
      {SLEEP_BLOCK_WATCHDOG_RESET, "boot followed a watchdog reset"},
      {SLEEP_BLOCK_STOP_FAULT, "reset inside Stop / clock not restored"},
      {SLEEP_BLOCK_NO_BACKSTOP, "no watchdog or RTC in Stop"},
      {SLEEP_BLOCK_USB, "USB attached"},
      {SLEEP_BLOCK_BOOT_GRACE, "first minute after boot"},
      {SLEEP_BLOCK_ACTIVITY, "recent button/console input"},
      {SLEEP_BLOCK_RADIO, "radio on (allow with console 2)"},
      {SLEEP_BLOCK_BUSY, "busy"},
      {SLEEP_BLOCK_DEBUGGER, "debugger attached"},
  };
  static const Name BUSY[] = {
      {SLEEP_BUSY_ACTUATOR, "actuator"}, {SLEEP_BUSY_BUTTONS, "buttons"},
      {SLEEP_BUSY_FLASH, "flash"},       {SLEEP_BUSY_OTA, "update"},
      {SLEEP_BUSY_LINK, "phone link"},   {SLEEP_BUSY_SERIAL, "console line"},
  };
  bool first = true;
  for (const Name& name : BLOCKS) {
    if ((blocks & name.bit) == 0) {
      continue;
    }
    out.print(first ? F("") : F("; "));
    out.print(name.text);
    first = false;
    if (name.bit == SLEEP_BLOCK_BUSY) {
      out.print(F(":"));
      for (const Name& item : BUSY) {
        if ((busy & item.bit) != 0) {
          out.print(' ');
          out.print(item.text);
        }
      }
    }
  }
}

void WatchPower::printStatus(Print& out, bool radio_on, uint8_t busy) {
  const uint32_t now = millis();
  out.println();
  out.println(F("--- sleep policy ---"));
  out.print(F("Deep sleep setting: "));
  out.print(static_cast<unsigned>(mode_));
  out.println(mode_ == DeepSleepMode::Off
                  ? F(" (off)")
                  : mode_ == DeepSleepMode::RadioOffOnly
                        ? F(" (Stop 2 while Bluetooth is off)")
                        : F(" (Stop 2 also with the radio on)"));
  out.print(F("USB attached (VDDUSB monitor): "));
  out.print(!usb_monitor_ok_ ? F("monitor unavailable, assumed yes")
                             : usbPowered() ? F("yes") : F("no"));
  out.print(F("; plug/unplug edges seen: "));
  out.println(usb_supply_changes_);
  out.print(F("Radio on: "));
  out.println(radio_on ? F("yes") : F("no"));

  const uint16_t blocks = blockers(radio_on, busy);
  out.print(F("Deep sleep allowed now: "));
  if (blocks == 0) {
    out.println(F("yes"));
  } else {
    out.print(F("no - "));
    printBlockers(out, blocks, busy);
    out.println();
  }
  out.print(F("Blockers at the last sleep: "));
  if (last_blocks_ == 0) {
    out.println(F("none"));
  } else {
    // The busy detail is the current one; the bit itself is historical.
    printBlockers(out, last_blocks_, busy);
    out.println();
  }

  out.print(F("Fail-safe: watchdog reset "));
  out.print(reset_by_watchdog_ ? F("YES") : F("no"));
  out.print(F(", Stop fault "));
  out.print(stop_fault_ ? F("YES") : F("no"));
  out.print(F(", IWDG runs in Stop "));
  out.println(backstop_ok_ ? F("yes") : F("NO"));

  if (last_window_ms_ != 0) {
    const uint32_t asleep = last_window_stop_ms_ + last_window_idle_ms_;
    out.print(F("Last minute ("));
    printMs(out, last_window_ms_);
    out.print(F("): wakes "));
    out.print(last_window_wakes_);
    out.print(F(", Stop "));
    printMs(out, last_window_stop_ms_);
    out.print(F(", Sleep "));
    printMs(out, last_window_idle_ms_);
    out.print(F(", awake "));
    printMs(out, last_window_ms_ > asleep ? last_window_ms_ - asleep : 0);
    out.println();
  }
  {
    const uint32_t span = now - window_started_ms_;
    const uint32_t asleep = window_stop_ms_ + window_idle_ms_;
    out.print(F("This minute so far ("));
    printMs(out, span);
    out.print(F("): wakes "));
    out.print(window_wakes_);
    out.print(F(", Stop "));
    printMs(out, window_stop_ms_);
    out.print(F(", Sleep "));
    printMs(out, window_idle_ms_);
    out.print(F(", awake "));
    printMs(out, span > asleep ? span - asleep : 0);
    out.println();
  }
  {
    const uint32_t uptime = now - boot_ms_;
    const uint32_t asleep = stop_ms_total_ + idle_ms_total_;
    out.print(F("Since boot ("));
    printMs(out, uptime);
    out.print(F("): Stop "));
    printMs(out, stop_ms_total_);
    out.print(F(", Sleep "));
    printMs(out, idle_ms_total_);
    out.print(F(", awake "));
    printMs(out, uptime > asleep ? uptime - asleep : 0);
    out.println();
  }
  out.print(F("Stop entries "));
  out.print(stop_entries_);
  out.print(F(" (device reached Stop "));
  out.print(stop_system_);
  out.print(F("), abandoned "));
  out.print(stop_abandoned_);
  out.print(F(", unavailable "));
  out.print(stop_unavailable_);
  out.print(F("; Sleep entries "));
  out.println(idle_entries_);
  out.print(F("Stop wakes by RTC/button/accel/battery/radio/USB/other: "));
  for (uint8_t cause = 0; cause < CAUSE_COUNT; ++cause) {
    out.print(wake_cause_[cause]);
    out.print(cause + 1 < CAUSE_COUNT ? '/' : '\n');
  }

  out.print(F("Clock "));
  out.print(SystemCoreClock / 1000000UL);
  out.print(F(" MHz, CPU2 started "));
  out.print(LL_PWR_IsEnabledBootC2() ? F("yes") : F("no"));
  out.print(F(", RF wake clock "));
  out.print(LL_RCC_GetRFWKPClockSource() == LL_RCC_RFWKP_CLKSOURCE_LSE
                ? F("LSE")
                : F("not LSE"));
  out.print(F(", debugger "));
  out.println(debuggerAttached() ? F("attached") : F("no"));

  // 2 bits per pin: 3 = analog (lowest leakage), 0 = digital input.
  out.print(F("GPIO MODER A-E:"));
  for (GPIO_TypeDef* port : {GPIOA, GPIOB, GPIOC, GPIOD, GPIOE}) {
    out.print(F(" 0x"));
    out.print(port->MODER, HEX);
  }
  out.println();
  out.println(F("--------------------"));
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
