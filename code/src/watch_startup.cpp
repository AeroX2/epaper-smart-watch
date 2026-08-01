#include <Arduino.h>

// A resident bootloader must branch with PRIMASK set: enabling interrupts
// before Reset_Handler risks dispatching a pending exception into application
// code before .data/.bss exists. The C runtime calls preinit functions after
// initializing RAM and before Arduino setup, which is the first safe point to
// restore the architectural reset interrupt state.
extern "C" void watchApplicationEnableInterrupts() {
  __set_BASEPRI(0);
  __set_FAULTMASK(0);
  __enable_irq();
  __DSB();
  __ISB();
}

using PreinitFunction = void (*)();

extern "C" __attribute__((section(".preinit_array"), used))
PreinitFunction watch_application_preinit = watchApplicationEnableInterrupts;
