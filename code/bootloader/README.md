# STM32WB USB DFU bootloader

This resident bootloader is adapted from STMicroelectronics' official
`P-NUCLEO-WB55.Nucleo/Applications/USB_Device/DFU_Standalone` example in
STM32CubeWB commit `e7385e64d05a6c143e60b3890668d45f611f872e`. It enumerates as the standard STM32 `0483:DF11` DFU device and
exposes only the watch application region at `0x08010000` through USB DFU.
The retained ST files are distributed under [SLA0044](LICENSE.ST.md).

The bootloader occupies the first 64 KiB. At reset it decides, in this order:

1. USB DFU if the application requested it through RTC backup register 19, or
   PCB button 1 (bottom-left) is held during reset. The external flash is not
   touched on this path.
2. Otherwise, install a staged firmware update from the external SPI flash if
   one is waiting (see below).
3. Otherwise, start the application if its vector table is valid, else USB
   DFU.

## Staged updates from external flash

The watch application receives a new firmware image over BLE, stores it in the
external flash (U4, XT25F32F) and resets. The bootloader copies it into the
application slot. The layout, header and rules are the shared contract in
[`../EXTERNAL_FLASH.md`](../EXTERNAL_FLASH.md); the implementation is
`src/staged_update.cpp` with the header checks in `include/ota_format.h`.

What the bootloader does when it finds a header with the right magic, format
and header CRC, `ready` set, `result` still erased and a size of 8...761856
bytes:

1. Reads the staged image back and checks its CRC-32 and vector table. If
   either is wrong it writes `result` 2 or 3 and boots the existing
   application, which has not been touched.
2. Erases the internal pages the image covers, programs it and verifies the
   CRC-32 of the internal copy. A flash error or mismatch is retried once. The
   first page (vector table) is erased first and programmed last, so a
   half-written slot never looks like a valid application.
3. Writes `result` 1 and starts the new application. If both attempts fail it
   makes sure the slot has no valid vector table (so later resets also land in
   USB DFU rather than running a corrupt image), writes `result` 4 and stays
   in USB DFU.

| `result` | Meaning |
|---|---|
| `0xFFFFFFFF` | not attempted (erased) |
| `1` | installed |
| `2` | staged image CRC mismatch; application untouched |
| `3` | staged image is not a valid application; application untouched |
| `4` | internal program/verify failed twice; watch is in USB DFU |

`result` is the only thing the bootloader ever writes to the external flash.
If power is lost during the install, `result` is still erased and the next
boot repeats it.

Notes:

- The install runs before the Arduino runtime starts, with no HAL tick: the
  external flash is bit-banged (single-line SPI on PA3/PD3/PD4/PD5, PD6 and
  PD7 held high) and every wait is a bounded loop. A missing or dead flash
  chip costs about 20 ms and then the normal boot continues.
- A full-size image takes roughly 15-20 s to install (the system clock is
  raised to MSI 32 MHz for the duration and restored afterwards). The display
  keeps its last image and nothing is shown, so the application should tell
  the user before it resets. Interrupting the install is safe; it restarts on
  the next boot.
- The motor, buzzer, LED/boost and display-reset pins stay in their safe
  states throughout, and the flash pins are returned to their reset (analog)
  state before the application starts.
- **Watches that already carry the older bootloader must have this one
  flashed once over SWD or the ROM USB DFU** (`-e watch_dfu_bootloader_stlink`
  or `-e watch_dfu_bootloader`). The resident bootloader cannot replace itself:
  it only exposes the application slot. Until then the application sees
  `ready` set with `result` still erased after the reset and reports
  `OTA_STATE|FAILED|0`.

Build it independently from the Arduino application:

```text
cd code/bootloader
pio run -e watch_dfu_bootloader
```

The direct application (`-e watch_direct`) remains available as a recovery
build; it is no longer the default because flashing it replaces this loader.
