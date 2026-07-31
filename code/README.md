# Smart-watch peripheral bring-up firmware

This firmware targets the current `STM32WB5MMG` schematic. It provides a USB CDC command console,
non-destructive peripheral checks, explicit ET011TJ1 display bring-up tests, and an initial
on-device watch interface for:

- BMA400 accelerometer over I2C
- BME280/BMP280 environmental sensor over I2C
- MAX17048 fuel gauge over I2C
- XT25F32 32-Mbit external flash over hardware Quad-SPI
- three capacitive-touch electrodes through the STM32 TSC
- four active-low buttons and the accelerometer/fuel-gauge interrupt lines
- vibration motor and buzzer, on explicit commands only
- ET011TJ1 reset/BUSY logic and 240x240 full-refresh patterns
- a circular 240x240 one-bit framebuffer and nine navigable watch screens

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

Pattern tests never run automatically. Start with `e`, which only resets the controller and checks
that active-low `BUSY_N` returns high; it leaves the high-voltage booster off. Then press `x`
immediately before `d` for a bordered diagnostic pattern, `w` for white, or `k` for black. Press
`l` to toggle the AP3032-driven display LED ring; it remains off at boot. The driver uses
four-wire SPI mode 0 at 4 MHz.

## Watch UI prototype

The watch UI remains dormant at boot so a bad or disconnected display cannot interfere with USB
DFU recovery. Press `u` in the console or press any watch button to render it for the first time.
Once active on the clock screen, it refreshes at minute boundaries. Every refresh returns the
ET011TJ1 to booster-off standby.

The four active-low buttons currently map as follows:

| PCB button | Physical corner | Action |
| --- | --- | --- |
| 2 | Top-left | Browse: previous card. Inside: previous value/item |
| 3 | Top-right | Browse: next card. Inside: next value/item |
| 1 | Bottom-left | Inside: back to card. Browse: return to clock |
| 4 | Bottom-right | Clock: open carousel. Browse: enter card. Inside: activate |

Console equivalents are `[` for previous, `]` for next, `m` for home, `o` for select, and `u` to
redraw the current screen. Button inputs use a 35 ms debounce interval. PB4 is restored to its
button input mode immediately after the display SPI core temporarily uses it as the required dummy
MISO pin.

Navigation has two explicit levels. A card says `SELECT OPEN` while browsing and shows `IN` after
entry. This keeps the button meanings deterministic: PREV/NEXT never trigger an action while
browsing, and never leave the current card while inside it.

Implemented screens mirror the browser playground: clock, notification, timer, alarms, music,
weather, sensors, activity, and settings. Temperature, humidity, pressure, acceleration, battery
percentage, battery voltage, time, timer state, alarms, and steps are live watch data. Phone
notifications, forecast data, time sync, and music metadata/control use the companion BLE link.

The STM32 RTC uses the fitted 32.768 kHz LSE and retains time through ordinary MCU resets while
backup power remains present. If LSE startup fails, firmware falls back to LSI so recovery and
USB remain usable, but BLE startup is blocked. An unset RTC is seeded from firmware build time.
Use `j` to inspect it or enter `@YYYY-MM-DDTHH:MM:SS` followed by Enter to set it manually.

The timer is adjusted in one-minute steps on its screen. Scheduled weekday/weekend alarms,
five-minute snooze, quiet mode, vibration, and alarm overrides for the buttons are active.
The step counter samples the BMA400 and uses a refractory peak detector; it is intentionally an
initial algorithm to calibrate against real walking tests, not a finished activity metric.

## Bluetooth and companion app

BLE starts only when explicitly requested, which preserves the known-safe boot/DFU recovery path.
Press `r` in the serial console or select Bluetooth in Settings. The watch advertises as
`E-Paper Watch`. STM32WB requires a compatible wireless coprocessor image; if startup fails,
install the matching `stm32wbxx_BLE_HCILayer_fw.bin` on CPU2/FUS.

The custom GATT service is `7BD10000-6B10-4A21-9D6A-3A56B35C1000`:

| Direction | Characteristic | Limit |
| --- | --- | ---: |
| Phone to watch | `7BD10001-6B10-4A21-9D6A-3A56B35C1000` | 160 bytes |
| Watch to phone | `7BD10002-6B10-4A21-9D6A-3A56B35C1000` | 80 bytes |

It also exposes the standard Battery Service. The current protocol is bounded UTF-8 text for easy
bring-up:

| Message | Purpose |
| --- | --- |
| `TIME,<epoch-seconds>` | Set the RTC |
| `NOTIFY,<app>\|<title>\|<body>` | Show a notification |
| `CLEAR_NOTIFICATION` | Clear the notification card |
| `WEATHER,<current>,<high>,<low>\|<condition>` | Update phone forecast |
| `MUSIC,<playing>,<position>,<duration>\|<title>\|<artist>` | Update media card |
| `STEPS,RESET` | Reset the in-RAM step count |
| `SLEEP,START`, `SLEEP,STOP`, `SLEEP,PAUSE,<epoch>` | Sleep movement control |
| `SLEEP,ALARM,<delay-ms>`, `SLEEP,ALARM_STOP` | Sleep alarm control |

Watch-to-phone messages include notification dismissal, media previous/play-pause/next, Sleep
snooze/dismiss, and `SLEEP,DATA,<max-delta-mg>` every ten seconds while sleep tracking is active.
See the [Android companion documentation](../companion/README.md) for setup.

Before running `d`, `w`, or `k`, verify that display bus-select jumper JP1 bridges BS to GND
(pins 1-2) for four-wire SPI. If the reset probe passes but power-on or refresh times out, inspect
the GDR/RESE booster circuit and generated rails. The current schematic uses a 2.2 uH L1 while the
ET011TJ1 reference circuit in `spooky.pdf` specifies 10 uH, so L1 is the first component value to
verify if the controller logic works but the booster does not.

The external-flash test only reads its JEDEC identity. It never programs or erases the chip.

## First hardware test sequence

1. Flash `code/.pio/build/watch/firmware.bin` through the normal DFU flow.
2. Confirm the automatic peripheral test completes and `j` reports a plausible retained time.
3. Press `u` once. Use the top buttons (PCB 2/3) to browse the eight cards; bottom-left (PCB 1)
   returns to the clock.
4. Browse to Timer, press bottom-right (PCB 4) to enter it, adjust with the top buttons, then press
   bottom-right to start. Bottom-left backs out to the Timer card; a second press returns to the clock. Verify vibration/buzzer at zero,
   button 4 snoozes, and button 1 dismisses.
5. Set a near-future alarm with `!HH:MM`, then test the same alarm controls.
6. Press `r`; wait for `BLE advertising as "E-Paper Watch"`.
7. Install/open the companion, grant nearby-device and notification permissions, and Connect.
8. Test phone time, the demo notification, weather values, live Android notifications, and media
   controls in that order.
9. Start the companion's test tracking and watch serial for a movement sample every ten seconds.

Do not judge battery life from this build. It still polls the accelerometer at 20 Hz and has not
yet moved the MCU/radio into the final low-power state machine.
