# STM32WB USB DFU bootloader

This resident bootloader is adapted from STMicroelectronics' official
`P-NUCLEO-WB55.Nucleo/Applications/USB_Device/DFU_Standalone` example in
STM32CubeWB commit `e7385e64d05a6c143e60b3890668d45f611f872e`. It enumerates as the standard STM32 `0483:DF11` DFU device and
exposes only the watch application region at `0x08010000` through USB DFU.
The retained ST files are distributed under [SLA0044](LICENSE.ST.md).

The bootloader occupies the first 64 KiB. It starts the application unless:

- the application requested DFU through RTC backup register 19;
- PCB button 1 (bottom-left) is held during reset; or
- the application vector table is invalid.

Build it independently from the Arduino application:

```text
cd code/bootloader
pio run -e watch_dfu_bootloader
```

The direct application remains the recovery/default build until a combined
bootloader plus relocated-application image has passed on-hardware testing.
