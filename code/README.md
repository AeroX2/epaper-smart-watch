# Smart-watch peripheral bring-up firmware

This firmware targets the current `STM32WB5MMG` schematic. It provides a USB CDC command console,
non-destructive peripheral checks, explicit ET011TJ1 display bring-up tests, and an initial
on-device watch interface for:

- BMA400 accelerometer over I2C
- BME280 environmental sensor over I2C
- MAX17048 fuel gauge over I2C
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
| Vibration motor | PA15 |
| Buzzer | PE0 |
| Display SPI SCK / MOSI / CS | PA5 / PA7 / PA4 |
| Display DC / RST_N / BUSY_N | PA8 / PA6 / PA2 |
| Display frontlight control | PC3 |

The external 32-Mbit XT25F32 QSPI flash is not used by the current firmware.
Firmware updates go directly from USB DFU into the STM32 application slot, so
the external flash and QSPI peripheral remain off.

## Build, bootstrap, and update over USB

The resident bootloader occupies `0x08000000`-`0x0800FFFF`; the normal watch
application is linked at `0x08010000`. The loader is based on ST's official
STM32CubeWB `DFU_Standalone` example, enumerates as `0483:DF11`, and exposes only
the application slot. Its flash callbacks also reject access outside
`0x08010000`-`0x080C9FFF`, protecting both the loader and the STM32WB wireless
stack reservation.

Build the loader, relocated application, and combined one-time factory image:

```text
cd bootloader
pio run --environment watch_dfu_bootloader
cd ..
pio run --environment watch
python tools/make_factory_image.py bootloader/.pio/build/watch_dfu_bootloader/firmware.bin .pio/build/watch/firmware.bin .pio/build/factory/epaper-watch-factory.bin
```

Program `epaper-watch-factory.bin` once at `0x08000000`, using BOOT0 plus
STM32CubeProgrammer or an ST-Link. This one physical bootstrap is unavoidable:
the resident loader must exist before the running application can request it.
The default `watch_direct` environment remains a known-safe address-zero
recovery build. BOOT0 and SWD continue to work as recovery paths.

After the factory image is installed, close any serial monitor, build the
relocated application, and update directly over USB:

```text
pio run --environment watch
python tools/usb_dfu_update.py COM26 .pio/build/watch/firmware.bin
```

The updater validates that the image is linked for `0x08010000`, sends `f` to
the watch over USB CDC, waits for the resident ST DFU device, then uses
STM32CubeProgrammer to erase, program, verify, and start the application. No
external-flash staging or custom host/device transfer protocol is involved.
Holding PCB button 1 (bottom-left) during reset also enters resident DFU if an
application update is interrupted or the application vector table is invalid.

This prototype does not authenticate firmware. Anyone with physical USB access
can install a correctly linked application; signed-image verification is a
separate production-hardening task.

For one-time SWD installation or debugging with a connected ST-Link probe, use
`watch_dfu_bootloader_stlink`, `watch_stlink`, or `watch_direct_stlink` as
appropriate. Never program only `watch` at address zero: it is deliberately
linked for `0x08010000`.

The watch exposes its logs and command prompt as a native USB serial device. The baud setting is
ignored by USB CDC, though the monitor is configured for 115200 baud for consistency.

## Firmware structure

`main.cpp` is only the Arduino entry point. `WatchApplication` owns and coordinates the hardware
and feature modules, while step/sleep sampling and alarm/timer output live in `WatchActivity` and
`WatchAlarm`. `WatchPower` owns the MCU sleep and wake policy. Display refresh policy, UI drawing,
buttons, BLE, RTC, diagnostics, sensors, and the ET011 controller each remain in their own driver
or controller files.

## Power and wake model

Normal operation is interrupt-driven. After servicing any queued work, CPU1 enters STM32 Sleep
mode through STM32LowPower; SysTick is suspended while asleep and the hardware RTC restores the
elapsed `millis()` time after wake. The application no longer runs a continuously polling loop.

The current wake sources are:

| Source | Wake reason |
| --- | --- |
| Four button EXTI lines | Start/continue debounce, hold, or navigation handling |
| BMA400 INT1 | Hardware step event; data-ready samples only during active sleep tracking |
| MAX17048 ALERT | Refresh the fuel-gauge state |
| STM32 RTC | Minute/alarm/timer progress and a one-second safety deadline |
| USB interrupt | Serial console input |
| STM32WB IPCC/radio interrupt | Pending BLE controller traffic |

Buttons are sampled at 10 ms only while a debounce, press, hold, or chord is in progress. Alarm
outputs also use the 10 ms deadline while active. At all other times the core sleeps until an
external interrupt or the one-second RTC deadline. `BLE.poll()` only drains controller events after
a wake; it is no longer called by a free-running busy loop.

The BMA400's internal step engine replaces the old 20 Hz software step detector. Its data-ready
interrupt is normally disabled and is enabled only for a Sleep as Android tracking session, where
movement samples are required. The BME280 is left in its library's forced-measurement mode and is
read only when live UI sensor data is rendered. The fuel gauge is read for UI data or in response
to its ALERT line. The display booster and frontlight remain off between explicit refreshes.

Sleep mode is intentional at this stage: it preserves USB CDC and the STM32WB radio/IPCC path while
still stopping CPU instruction execution and SysTick between events. STOP2 can reduce current
further, but should be introduced only with explicit clock, USB, and BLE resume testing on hardware.

## Console commands

`a` runs every safe automatic test, `s` prints sensor values, `i` scans I2C, and `g` prints GPIO
states. Use `v` for a short vibration pulse
and `b` for a buzzer chirp. Press `h` to print the command list on the device.

Press `f` to shut down the display booster, LED ring, vibration, buzzer, and BLE,
then reboot into resident STM32 USB DFU. Ordinarily `tools/usb_dfu_update.py`
sends this command and invokes STM32CubeProgrammer automatically. The direct
recovery build deliberately refuses `f` because it has no resident loader.

Pattern tests never run automatically. Start with `e`, which only resets the controller and checks
that active-low `BUSY_N` returns high; it leaves the high-voltage booster off. Then press `x`
immediately before `d` for a bordered diagnostic pattern, `w` for white, or `k` for black. Press
`l` to toggle the AP3032-driven display LED ring; it remains off at boot. The driver uses
four-wire SPI mode 0 at 4 MHz.

All pattern and watch-UI updates now use the driver ported from
`Ardiuno_ET011TJ2_hspi_01.zip`. The archive is internally named ET011TT6. The port uses the supplied
registers, 672-byte LUT, and DRF mode `0x08`, while retaining the board's known-good `0x25` VCOM
value and correcting the source sketch's one-byte DTM overflow. Display transmission converts the
firmware framebuffer to the supplied sketch's polarity (`0xFF` white and `0x00` black). Pattern
commands still require `x` before every high-voltage update.

## Watch UI prototype

At normal boot the watch clears the panel to white and renders the clock face. Press `u` in the
console to redraw the current screen using the ordinary UI refresh policy.
Press uppercase `U` to force a cleaning full refresh of the current UI (`white -> UI`) immediately;
the same cleaning sequence also runs automatically after ten ordinary UI updates.
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
The step counter uses the BMA400's hardware step engine and interrupt rather than periodic raw
acceleration sampling. Resetting steps records a new sensor-counter baseline without resetting the
accelerometer itself.

## Bluetooth and companion app

BLE starts only when explicitly requested, preserving the lowest-power default and the USB/SWD
recovery path.
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

## First hardware test sequence

1. Install the combined factory image once, then use `tools/usb_dfu_update.py` for later application builds.
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

Power behavior should still be measured on the assembled watch, particularly with BLE connected
and during Sleep as Android tracking. Those modes naturally wake more often than the idle clock.
