# Smart-watch peripheral bring-up firmware

This firmware targets the current `STM32WB5MMG` schematic. It provides a USB CDC command console,
non-destructive peripheral checks, and explicit ET011TJ1 display bring-up tests for:

- BMA400 accelerometer over I2C
- BME280/BMP280 environmental sensor over I2C
- MAX17048 fuel gauge over I2C
- XT25F32 32-Mbit external flash over hardware Quad-SPI
- three capacitive-touch electrodes through the STM32 TSC
- four active-low buttons and the accelerometer/fuel-gauge interrupt lines
- vibration motor and buzzer, on explicit commands only
- ET011TJ1 reset/BUSY logic and 240x240 full-refresh patterns

The nPM1100 starts with `ISET=LOW` and `MODE=LOW`, keeping it in automatic buck mode and at the
conservative USB current setting. The bring-up firmware does not enter ship mode or raise the USB
current limit.

## Schematic pin map

| Function | MCU pin |
| --- | --- |
| I2C1 SCL / SDA | PB8 / PB9 |
| BMA400 INT1 / INT2 | PC0 / PC1 |
| MAX17048 alert | PB3 |
| Buttons 1–4 | PC10, PC2, PC12, PB4 |
| nPM1100 ISET / MODE / SHPHLD | PD0 / PA9 / PC6 |
| USB D- / D+ | PA11 / PA12 |
| QSPI CLK / CS | PA3 / PD3 |
| QSPI IO0–IO3 | PD4, PD5, PD6, PD7 |
| Touch sample capacitor | PB12 |
| Touch electrodes 1–3 | PB13, PB14, PB15 |
| Vibration motor | PA15 |
| Buzzer | PE0 |
| Display SPI SCK / MOSI / CS | PA5 / PA7 / PA4 |
| Display DC / RST_N / BUSY_N | PA8 / PA6 / PA2 |
| Display frontlight control | PC3 |

## Build and flash

The default environment uploads through the STM32 ROM USB DFU bootloader:

```text
pio run
pio run --target upload
pio device monitor
```

Windows should show `STM32 Bootloader` before the upload. If the application is
already programmed, BOOT0 must be asserted while resetting to re-enter the ROM
bootloader.

For SWD upload or debugging with a connected ST-Link probe, use:

```text
pio run --environment watch_stlink --target upload
```

The watch exposes its logs and command prompt as a native USB serial device. The baud setting is
ignored by USB CDC, though the monitor is configured for 115200 baud for consistency.

## Console commands

`a` runs every safe automatic test, `s` prints sensor values, `i` scans I2C, `f` reads the flash
JEDEC ID, `t` prints touch counts, and `g` prints GPIO states. Use `v` for a short vibration pulse
and `b` for a buzzer chirp. Press `h` to print the command list on the device.

Display operations never run automatically. Start with `e`, which only resets the controller and
checks that active-low `BUSY_N` returns high; it leaves the high-voltage booster off. Then use `d`
for a bordered diagnostic pattern, `w` for white, or `k` for black. Press `l` to toggle the
AP3032-driven display LED ring; it remains off at boot. The driver uses four-wire SPI mode 0 at
4 MHz.

Before running `d`, `w`, or `k`, verify that display bus-select jumper JP1 bridges BS to GND
(pins 1-2) for four-wire SPI. If the reset probe passes but power-on or refresh times out, inspect
the GDR/RESE booster circuit and generated rails. The current schematic uses a 2.2 uH L1 while the
ET011TJ1 reference circuit in `spooky.pdf` specifies 10 uH, so L1 is the first component value to
verify if the controller logic works but the booster does not.

The external-flash test only reads its JEDEC identity. It never programs or erases the chip.
