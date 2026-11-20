#include "watch_watchdog.h"

#include <IWatchdog.h>

#include "stm32yyxx_ll_lptim.h"

namespace {

// BKP12/13 hold the UTC offset, BKP15 the Stop marker, BKP19 the DFU request.
// BKP14: "CR" in the top half, watchdog resets in a row in the bottom byte.
#define WATCH_CRASH_REGISTER (RTC->BKP14R)
#define WATCH_DFU_REQUEST_REGISTER (RTC->BKP19R)
// BKP11: start-up stage breadcrumbs (not used by the core, STM32RTC or the rest).
#define WATCH_BOOT_STAGE_REGISTER (RTC->BKP11R)
constexpr uint32_t BOOT_STAGE_MAGIC = 0xB0070000UL;
#define WATCH_HANG_MAGIC_REGISTER (RTC->BKP16R)
#define WATCH_HANG_PC_REGISTER (RTC->BKP17R)
#define WATCH_HANG_LR_REGISTER (RTC->BKP18R)
constexpr uint32_t HANG_MAGIC = 0x48414E47UL;  // "HANG"
constexpr uint32_t CRASH_MAGIC = 0x43520000UL;  // "CR"
constexpr uint32_t CRASH_MAGIC_MASK = 0xFFFF0000UL;
// Same value as watch_dfu.cpp and the resident bootloader.
constexpr uint32_t DFU_REQUEST_MAGIC = 0x31465544UL;

volatile uint32_t feed_count = 0;
uint32_t seen_feed_count = 0;
uint32_t stale_ticks = 0;
uint32_t record_after_ticks = 0;
bool hang_recorded = false;

// The STM32 HAL I2C interrupt handler can return without clearing the flag
// that raised it (handle locked, or TXIS with nothing left to send). The
// interrupt then fires again at once, for ever, and the main code never runs
// again, so the Wire library's own 100 ms timeout cannot expire. An honest
// I2C interrupt lasts microseconds: finding the CPU inside one on two ticks
// in a row (131 ms apart) means a storm. Mask that I2C's interrupts so the
// main code resumes and times out; I2cBus restarts the bus afterwards.
volatile uint32_t i2c_storm_count = 0;
uint32_t i2c_stuck_ticks = 0;

void breakI2cStorm(uint32_t interrupted_exception) {
  I2C_TypeDef* stuck = nullptr;
  if (interrupted_exception == 16U + I2C1_EV_IRQn ||
      interrupted_exception == 16U + I2C1_ER_IRQn) {
    stuck = I2C1;
  } else if (interrupted_exception == 16U + I2C3_EV_IRQn ||
             interrupted_exception == 16U + I2C3_ER_IRQn) {
    stuck = I2C3;
  }
  if (stuck == nullptr) {
    i2c_stuck_ticks = 0;
    return;
  }
  if (++i2c_stuck_ticks < 2) {
    return;
  }
  i2c_stuck_ticks = 0;
  stuck->CR1 &= ~(I2C_CR1_TXIE | I2C_CR1_RXIE | I2C_CR1_ADDRIE |
                  I2C_CR1_NACKIE | I2C_CR1_STOPIE | I2C_CR1_TCIE |
                  I2C_CR1_ERRIE);
  ++i2c_storm_count;
}

}  // namespace

uint32_t watchI2cStormCount() {
  return i2c_storm_count;
}

extern "C" {

// frame = the exception stack frame: r0-r3, r12, lr, pc, xpsr.
void watchHangTick(const uint32_t* frame) {
  LL_LPTIM_ClearFLAG_ARRM(LPTIM1);
  breakI2cStorm(frame[7] & 0x1FFU);
  const uint32_t feeds = feed_count;
  if (feeds != seen_feed_count) {
    seen_feed_count = feeds;
    stale_ticks = 0;
    hang_recorded = false;
    return;
  }
  if (hang_recorded || ++stale_ticks < record_after_ticks) {
    return;
  }
  hang_recorded = true;
  LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_RTCAPB);
  LL_PWR_EnableBkUpAccess();
  WATCH_HANG_PC_REGISTER = frame[6];
  WATCH_HANG_LR_REGISTER = frame[5];
  WATCH_HANG_MAGIC_REGISTER = HANG_MAGIC;
}

__attribute__((naked)) void LPTIM1_IRQHandler(void) {
  __asm volatile(
      "tst lr, #4\n"
      "ite eq\n"
      "mrseq r0, msp\n"
      "mrsne r0, psp\n"
      "b watchHangTick\n");
}

}  // extern "C"

void WatchWatchdog::startHangMonitor() {
  LL_RCC_SetLPTIMClockSource(LL_RCC_LPTIM1_CLKSOURCE_PCLK1);
  LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_LPTIM1);
  // PCLK1 / 128 with a full 16-bit period: about 7.6 ticks a second at 64 MHz.
  const uint32_t tick_hz_x16 = (HAL_RCC_GetPCLK1Freq() / 128UL) / 4096UL;
  record_after_ticks = (HANG_RECORD_MS / 1000UL) * tick_hz_x16 / 16UL;
  if (record_after_ticks < 8) {
    record_after_ticks = 8;
  }
  LL_LPTIM_Disable(LPTIM1);
  LL_LPTIM_SetClockSource(LPTIM1, LL_LPTIM_CLK_SOURCE_INTERNAL);
  LL_LPTIM_SetPrescaler(LPTIM1, LL_LPTIM_PRESCALER_DIV128);
  LL_LPTIM_EnableIT_ARRM(LPTIM1);  // only writable while disabled
  LL_LPTIM_Enable(LPTIM1);
  LL_LPTIM_SetAutoReload(LPTIM1, 0xFFFF);
  LL_LPTIM_StartCounter(LPTIM1, LL_LPTIM_OPERATING_MODE_CONTINUOUS);
  HAL_NVIC_SetPriority(LPTIM1_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(LPTIM1_IRQn);
}

void WatchWatchdog::begin() {
  // isReset(true) also clears the RCC reset flags so the next boot reports
  // only its own cause.
  reset_by_watchdog_ = IWatchdog.isReset(true);

  // Keep the IWDG paused while an SWD debugger has the core halted, so
  // breakpoints do not reset the watch.
  __HAL_DBGMCU_FREEZE_IWDG();

  IWatchdog.begin(TIMEOUT_MS * 1000UL);

  LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_RTCAPB);
  LL_PWR_EnableBkUpAccess();

  // Any other reset cause (power, pin, software, firmware update) ends the
  // run of watchdog resets.
  const uint32_t crash = WATCH_CRASH_REGISTER;
  uint32_t resets = 0;
  if (reset_by_watchdog_) {
    resets = (crash & CRASH_MAGIC_MASK) == CRASH_MAGIC ? (crash & 0xFFU) : 0;
    if (resets < 0xFFU) {
      ++resets;
    }
  }
  resets_in_a_row_ = static_cast<uint8_t>(resets);
  WATCH_CRASH_REGISTER = CRASH_MAGIC | resets;
#ifdef WATCH_HAS_RESIDENT_DFU
  if (resets >= CRASH_LOOP_RESETS) {
    // Start counting afresh after the update; keep the hang location, which
    // the next application boot can no longer print as a watchdog reset.
    WATCH_CRASH_REGISTER = CRASH_MAGIC;
    WATCH_DFU_REQUEST_REGISTER = DFU_REQUEST_MAGIC;
    __DSB();
    NVIC_SystemReset();
    while (true) {
    }
  }
#endif

  if (WATCH_HANG_MAGIC_REGISTER == HANG_MAGIC) {
    // Also kept when this boot did not follow the watchdog reset itself:
    // after three resets in a row the watch goes to USB DFU, and the boot
    // after a reflash is the first chance to report where it hung.
    hang_pc_ = WATCH_HANG_PC_REGISTER;
    hang_lr_ = WATCH_HANG_LR_REGISTER;
    WATCH_HANG_MAGIC_REGISTER = 0;
  }
  // Where the previous boot's setup() got to, if it never finished.
  const uint32_t stage = WATCH_BOOT_STAGE_REGISTER;
  if ((stage & 0xFFFF0000UL) == BOOT_STAGE_MAGIC &&
      (stage & 0xFFU) != BOOT_STAGE_DONE) {
    last_boot_stage_ = static_cast<uint8_t>(stage & 0xFFU);
  }
  // The location registers keep the last recorded hang until the radio's
  // backup-domain reset clears them, even after an earlier boot consumed
  // the magic: worth printing as a hint.
  stale_hang_pc_ = WATCH_HANG_PC_REGISTER;
  stale_hang_lr_ = WATCH_HANG_LR_REGISTER;
  startHangMonitor();
}

void WatchWatchdog::service() {
  if (healthy_ || millis() < HEALTHY_AFTER_MS) {
    return;
  }
  healthy_ = true;
  LL_PWR_EnableBkUpAccess();
  WATCH_CRASH_REGISTER = CRASH_MAGIC;
}

void WatchWatchdog::bootStage(uint8_t stage) {
  LL_PWR_EnableBkUpAccess();
  WATCH_BOOT_STAGE_REGISTER = BOOT_STAGE_MAGIC | stage;
}

void WatchWatchdog::feed() {
  IWatchdog.reload();
  ++feed_count;
}

void WatchWatchdog::printStatus(Print& out) const {
  out.print(F("Watchdog: "));
  out.print(TIMEOUT_MS / 1000UL);
  out.println(F(" s timeout"));
  if (reset_by_watchdog_) {
    out.println(F(
        "[WARN] Previous reset was caused by the watchdog: the firmware hung."));
    if (hang_pc_ != 0) {
      out.print(F("[WARN] It was stuck at PC=0x"));
      out.print(hang_pc_, HEX);
      out.print(F(" LR=0x"));
      out.println(hang_lr_, HEX);
    } else {
      out.println(F("[WARN] The hang monitor recorded no location."));
    }
    out.print(F("[WARN] Watchdog resets in a row: "));
    out.print(resets_in_a_row_);
    out.print(F(" (USB DFU at "));
    out.print(CRASH_LOOP_RESETS);
    out.println(F(")"));
  } else if (hang_pc_ != 0) {
    out.print(F("[WARN] A hang was recorded before an earlier reset: PC=0x"));
    out.print(hang_pc_, HEX);
    out.print(F(" LR=0x"));
    out.println(hang_lr_, HEX);
  }
  if (last_boot_stage_ != 0) {
    out.print(F("[WARN] The previous start-up stopped at stage "));
    out.println(last_boot_stage_);
  }
  if (hang_pc_ == 0 && stale_hang_pc_ != 0 && stale_hang_pc_ != 0xFFFFFFFFUL) {
    out.print(F("Hang monitor registers (last hang, already reported): PC=0x"));
    out.print(stale_hang_pc_, HEX);
    out.print(F(" LR=0x"));
    out.println(stale_hang_lr_, HEX);
  }
}
