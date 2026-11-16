#include "watch_watchdog.h"

#include <IWatchdog.h>

void WatchWatchdog::begin() {
  // isReset(true) also clears the RCC reset flags so the next boot reports
  // only its own cause.
  reset_by_watchdog_ = IWatchdog.isReset(true);

  // Keep the IWDG paused while an SWD debugger has the core halted, so
  // breakpoints do not reset the watch.
  __HAL_DBGMCU_FREEZE_IWDG();

  IWatchdog.begin(TIMEOUT_MS * 1000UL);
}

void WatchWatchdog::feed() {
  IWatchdog.reload();
}

void WatchWatchdog::printStatus(Print& out) const {
  out.print(F("Watchdog: "));
  out.print(TIMEOUT_MS / 1000UL);
  out.println(F(" s timeout"));
  if (reset_by_watchdog_) {
    out.println(F(
        "[WARN] Previous reset was caused by the watchdog: the firmware hung."));
  }
}
