/*
 * Install a staged firmware update from the external SPI flash
 * (code/EXTERNAL_FLASH.md, "Bootloader behaviour").
 */
#pragma once

enum class StagedUpdateOutcome {
  kNone,          /* no chip, or no update waiting: nothing was changed */
  kInstalled,     /* result 1: application slot now holds the staged image */
  kRejected,      /* result 2/3: staged copy bad, application untouched */
  kInstallFailed  /* result 4: application slot invalidated, stay in DFU */
};

/* Safe to call from .preinit_array: uses no HAL, no SysTick, no interrupts
 * and no static storage. Every wait is a bounded loop. On return the system
 * clock, flash latency and the external-flash pins are back in the state they
 * were in on entry. */
StagedUpdateOutcome runStagedUpdate();
