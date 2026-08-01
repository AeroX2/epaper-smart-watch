#include "watch_dfu.h"

#include "stm32wbxx_hal.h"

namespace {

constexpr uint32_t DFU_REQUEST_MAGIC = 0x31465544UL;

}  // namespace

bool WatchDfu::available() const {
#ifdef WATCH_HAS_RESIDENT_DFU
  return true;
#else
  return false;
#endif
}

void WatchDfu::request(Print& out) const {
#ifdef WATCH_HAS_RESIDENT_DFU
  HAL_PWR_EnableBkUpAccess();
  RTC->BKP19R = DFU_REQUEST_MAGIC;
  __DSB();
  (void)out;
  NVIC_SystemReset();
  while (true) {
  }
#else
  out.println(F("[BLOCKED] This direct recovery image has no resident DFU bootloader."));
#endif
}
