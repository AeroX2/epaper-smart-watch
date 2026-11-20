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

The external 32-Mbit XT25F32F flash (U4) on the QSPI pins holds the settings, the bond key, the
step history and staged Bluetooth firmware updates; see "External flash storage" below and
[`EXTERNAL_FLASH.md`](EXTERNAL_FLASH.md). The QUADSPI peripheral itself stays off: the firmware
bit-bangs single-line SPI on those pins. USB DFU still writes straight into the STM32 application
slot and remains the recovery path.

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
The default environment is `watch` (linked for `0x08010000`), so a bare
`pio run -t upload` cannot overwrite the loader. `watch_direct` remains an
address-zero recovery build that replaces the loader; select it explicitly with
`-e watch_direct`. BOOT0 and SWD continue to work as recovery paths.

After the factory image is installed, close any serial monitor, build the
relocated application, and update directly over USB:

```text
pio run --environment watch
python tools/usb_dfu_update.py COM26 .pio/build/watch/firmware.bin
```

The updater validates that the image is linked for `0x08010000`, sends `f` to
the watch over USB CDC, waits for the resident ST DFU device, then uses
`dfu-util` (from PATH, or the copy PlatformIO installs) to erase, program, read
back and start the application. STM32CubeProgrammer v2.23 fails to erase through
the resident loader, so it is not used here. The
USB path involves no external-flash staging (that is only used by the Bluetooth
update described below).
Holding PCB button 1 (top-left) during reset also enters resident DFU if an
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
`WatchAlarm`. `WatchPower` owns the MCU sleep and wake policy (Stop 2 or Sleep mode, see below). Display refresh policy, UI drawing,
buttons, BLE, RTC, diagnostics, sensors, and the ET011 controller each remain in their own driver
or controller files.

## Power and wake model

Nothing in this section has been measured on hardware yet. The mechanisms are described as
implemented; every current figure is an estimate from datasheet typicals.

### Sleep model

After servicing whatever woke it, the main loop asks `WatchPower::sleep()` to sleep until the next
real deadline (`WatchApplication::sleepBudgetMs()`), and `WatchPower` picks one of two modes:

| Mode | What it is | When | Longest sleep |
| --- | --- | --- | --- |
| **Stop 2** ("deep sleep") | All high-speed clocks off, RTC on LSE, RAM and registers retained | No blocker in the table below | 10 s (the 20 s IWDG keeps counting in Stop) |
| **Sleep** (`LowPower.idle()`) | CPU halted, 64 MHz clock tree, SMPS and all peripherals running | Any blocker set | 1 s, as before |

`millis()` is corrected from the RTC after either mode, so debounce, alarms and protocol timers
see continuous time. The deadline is the earliest of: 10 ms while a button is being debounced/held,
an actuator pulse or tone is running, step history is being sent or a message to the phone is
pending; 250 ms while a firmware update is in progress; the next minute boundary (only while the
clock face is displayed); timer expiry, snooze end, today's alarm, local
midnight; the 10-minute `STATUS` (only with a phone session); the 10-minute background step read
and the hourly step save; the end of the fast-advertising window; the front-light auto-off.

Wake sources in Stop 2:

| Source | Line | Purpose |
| --- | --- | --- |
| RTC alarm A | EXTI 17 | Deadline / watchdog feed. Always re-armed and verified before every Stop |
| Buttons | EXTI 2, 4, 10, 12 | Input |
| BMA400 INT1 | EXTI 0 | Motion gestures (double tap, wrist turn), latched |
| MAX17048 ALRT | EXTI 3 | Fuel-gauge alert |
| VDDUSB monitor (PVM1) | EXTI 31 | USB plugged in or removed |
| IPCC CPU1 receive | EXTI 36 | BLE controller traffic (only relevant with the radio on) |

### What blocks deep sleep

`z` on the console prints the current state of every item.

| Blocker | Meaning |
| --- | --- |
| Setting | Deep sleep switched off with console `0` |
| Watchdog reset | This boot followed an IWDG reset |
| Stop fault | The previous reset happened inside a Stop attempt, or a wake did not restore the PLL |
| No backstop | Option bytes freeze the IWDG in Stop, or the RTC is not running |
| USB | VDDUSB is present (its LDO is only powered from the connector), or the monitor could not be enabled |
| Boot grace | First 60 s after boot |
| Activity | 30 s after a button press, 5 min after a console byte |
| Radio | Bluetooth is on and the setting is not `2` |
| Busy | Actuator/tone running, button held, external flash held awake, firmware update not idle, phone-link data in flight, console line half typed |
| Debugger | An SWD debugger is attached |

E-paper refreshes and flash operations are synchronous, so the loop never reaches the sleep call
during one; the BUSY waits inside a refresh use Sleep mode (WFI until the next 1 ms tick) rather
than spinning.

### Radio on: Sleep mode by default

With Bluetooth advertising or connected the default is **Sleep mode, not Stop**. The Stop entry
and exit follow ST's dual-core sequence (RCC and entry-stop-mode hardware semaphores, HSI16 as
wake-up clock, CPU2's own low-power request left alone once CPU2 runs, the IPCC receive line
enabled as a wake source), and the clock tree is rebuilt before any interrupt handler runs. But
this stack is not a configuration ST or STM32duinoBLE has tested: the generic WB5MMG variant did
not even select the radio's wake-up clock (now done in `WatchPower::selectRadioWakeClock()`), and
the CPU2 side of the handshake cannot be inspected. Until it has been verified on hardware
(checks below), Stop with the radio on is opt-in with console `2`.

After Bluetooth has been turned off again CPU2 keeps running idle. The watch then uses Stop 2
under the default setting, with the same dual-core sequence.

### Fail-safe ladder

1. The IWDG (20 s) runs in Stop. A Stop that never ends becomes a reset.
2. A boot that follows a watchdog reset never uses Stop.
3. A marker in RTC backup register 15 is set for the duration of every Stop attempt including the
   clock restart. Found set at boot, Stop is disabled for that boot whatever the reset cause.
4. Stop is refused unless the RTC alarm is enabled, routed through EXTI 17 and enabled in the
   NVIC, and still in the future; interrupts are masked from that check until the clocks are back,
   so a wake that arrives in between makes `WFI` fall straight through.
5. If the hardware semaphores cannot be taken, or the PLL is not the system clock after a wake,
   the watch falls back to Sleep mode (the latter for the rest of the boot).
6. USB: Stop is never used with VDDUSB present, for a minute after boot, for 30 s after a button
   press or for 5 min after console input. If the console is ever unreachable: press a button,
   plug in USB within 30 s, and send `0`.
7. Console `0` disables deep sleep and stores that in the external flash (`1` = default,
   `2` = also with the radio on). Without working flash the setting lasts until the next reset.

### Peripherals

- BMA400: normal mode, 100 Hz, OSR 0 (about 4 uA; its low-power mode cannot count steps). The step
  counter runs without an interrupt pin; `WatchActivity::pollSteps()` reads it before every render,
  `STATUS`, save and midnight roll-over, and every 10 minutes otherwise.
- BME280: forced mode, one conversion at most every 5 minutes.
- MAX17048: forced into hibernate (`HIBRT = 0xFFFF`, about 4 uA instead of 23 uA); read on render
  and for `STATUS`.
- ET011TJ1: after every update the booster is powered off and the controller held in reset with
  SPI1 disabled; SCK/MOSI are then driven low and BUSY_N is put in analog mode.
- Front light: off at boot, switches itself off 30 s after it was turned on or a button was last
  pressed.
- External flash: deep power-down except inside an operation or a firmware transfer.
- Vibration and buzzer gates are returned to plain GPIO low after every use (PWM timer released,
  tone timer paused with the pin forced low).
- Unused MCU pins are left in their reset state, which is analog mode on STM32WB; `z` prints the
  MODER registers.

### Bluetooth parameters

Advertising runs at 100 ms for 30 s after Bluetooth is turned on and after every disconnect, then
at 1022.5 ms. The connection interval is the phone's decision (the companion requests low-power
parameters once the session is up and fast ones only for a firmware transfer); the watch sends no
connection-parameter requests. TX power stays at the library default of about 0 dBm.

### Expected current (estimates, to be measured)

Battery current, 130 mAh cell. "Refresh" is one e-paper update per minute on the clock face.

| State | Estimate | Assumes |
| --- | --- | --- |
| Bluetooth off, static screen | 15-25 uA | Stop 2 with RTC about 3 uA; BMA400 4 uA; MAX17048 4 uA; flash, BME280, nPM1100, AP3032 about 1 uA each; display controller in reset a few uA (unknown) |
| Bluetooth off, clock face | 100-200 uA | The above plus one fast refresh a minute at roughly 6 mA*s (1.5 s of MCU Sleep mode plus booster and panel, both unmeasured) and a cleaning refresh every tenth update |
| Bluetooth on (default), any screen | 1-2 mA | MCU in Sleep mode at 64 MHz with HSI16, HSI48, MSI and PLL running |
| Bluetooth advertising, setting `2`, static screen | 40-70 uA | 1 s advertising interval at about 20-30 uA average |
| Bluetooth connected, setting `2`, clock face | 150-300 uA | Phone-chosen 100-125 ms interval with slave latency; refresh as above |
| USB attached | 1-2 mA plus charging | Sleep mode, by design |
| Front light on | add 3-5 mA | Until the 30 s auto-off |

### Hardware checks, in order

1. With USB attached, `z`: "USB attached: yes", and the IWDG line says it runs in Stop.
2. Send `0`, unplug USB, wait ten seconds, plug it back in, `z`: the console still works and
   "plug/unplug edges seen" went up by two. Only then send `1`.
3. Bluetooth off, on battery with a meter in series, a non-clock screen showing: wait 60 s after
   the last button press. Expect tens of microamps. Plug USB in and press `z`: Stop time should
   dominate "last minute", with about 6 wakes a minute, nearly all by RTC.
4. Buttons respond from deep sleep; the clock face redraws on the minute.
5. If the watch resets about 20 s after going quiet, `z` shows "watchdog reset YES" or
   "Stop fault YES" and it stays in Sleep mode: the RTC wake-up from Stop is not working.
6. Bluetooth on, setting `1`: pairing, notifications and firmware update behave as before, and
   `z` reports "RF wake clock LSE".
7. Only then try `2`: advertise, connect, leave it for an hour, send notifications, check `z`
   for wakes by radio and that "device reached Stop" keeps counting. Any link drop or missed
   message: back to `1`.

## Console commands

`a` runs every safe automatic test, `s` prints sensor values, `i` scans I2C, and `g` prints GPIO
states. Use `v` for a short vibration pulse
and `b` for a buzzer chirp. Press `h` to print the command list on the device.

`q` re-probes the external flash without writing to it and prints its JEDEC ID (expected
`0x0B4016`), status register, the config-store state (which sector copy and sequence number each
record uses, and how many writes this boot), the Bluetooth-update state, and the secure-link /
bond / encryption state. The same flash line is printed at boot. `t` plays the next notification
alert tone (1-5) regardless of settings. `p` forgets the bonded phone (erases the stored link key).

`z` prints the sleep policy: whether USB is attached, whether deep sleep is allowed and why not,
wakes and Stop/Sleep/awake time over the last minute and since boot, what ended each Stop, and the
fail-safe flags. `0` switches deep sleep off, `1` restores the default (Stop 2 only while Bluetooth
is off) and `2` also allows it with the radio on; the choice is stored in the external flash.

Press `f` to shut down the display booster, LED ring, vibration, buzzer, and BLE,
then reboot into resident STM32 USB DFU. Ordinarily `tools/usb_dfu_update.py`
sends this command and runs `dfu-util` automatically. The direct
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
ET011TJ1 to reset with its booster off.

An ordinary update takes about 0.75 s (`ET011TJ1::showFrameKw`): 0.17 s to release reset, load the
registers and send the old and new frames as 1-bpp data (DTM3 / DTM4), 0.5 s for a 12-frame
waveform, and 0.08 s to switch the booster off. The register set, data path and waveform come from
a bus capture of a product that drives this panel once a second; the source direction bit and the
gate driver settings stay at this board's values, otherwise the image is flipped. The refresh
command uses mode `0x00`: measured here, mode `0x01` (the datasheet's black/white mode) ignores the
table's length and runs 1.9 s. Console `y` switches ordinary updates back to the older GC fast
update (1.1 s: it powers the controller up twice and runs a 31-frame waveform) for comparison.

The four active-low buttons map as follows by default. The companion app can give each corner any
of the four roles (`BUTTONS` message, stored in external flash and restored at boot); everything
below describes the roles, wherever they sit:

| PCB button | Physical corner | Action |
| --- | --- | --- |
| 2 | Top-right | Next. Browse: next card. Inside: next value/item |
| 3 | Bottom-right | Previous. Browse: previous card. Inside: previous value/item |
| 4 | Bottom-left | Home: inside: back to card. Browse: return to clock. Clock: press twice within 0.6 s to switch the LED ring on or off |
| 1 | Top-left | Select: clock: open carousel; with notifications waiting, a double press within 0.6 s shows the newest one again as the pop-up (and a single press opens the carousel after that 0.6 s). Browse: enter card. Inside: activate |

Each press acts when the button is released. There are no long presses and no two-button gestures.

Motion gestures (BMA400 on INT1, latched, on the 100 Hz normal-mode path the step counter
already uses, so they add next to no current): a **double tap** on the face (Z axis, sensitivity 1 of 0-7 where 0 is the most
sensitive, taps 40-500 ms apart) shows the newest notification as the pop-up (or closes it); **turning the wrist to look** (orientation change
ending with the face roughly flat, |z| ≥ 0.75 g) lights the front light for 5 s between 19:00
and 06:00. A button press while it is lit gives it the normal 30 s.
While an alarm rings, home dismisses it and select snoozes it. Holding
PCB button 1 (top-left) during reset for DFU is a bootloader feature and does not move with the
layout.

Console equivalents are `[` for previous, `]` for next, `m` for home, `o` for select, and `u` to
redraw the current screen. Button inputs use a 35 ms debounce interval. PB4 is restored to its
button input mode immediately after the display SPI core temporarily uses it as the required dummy
MISO pin.

Navigation has two explicit levels. A card says `SELECT OPEN` while browsing and shows `IN` after
entry. This keeps the button meanings deterministic: PREV/NEXT never trigger an action while
browsing, and never leave the current card while inside it.

Implemented screens mirror the browser playground: clock, notification, timer, alarms, music,
activity, find phone, camera, and settings (the BME280 temperature, humidity and air pressure appear on the
clock face). Temperature, humidity, pressure, acceleration, battery
percentage, battery voltage, time, timer state, alarms, and steps are live watch data. Phone
notifications, time sync, and music metadata/control use the companion BLE link.

The STM32 RTC uses the fitted 32.768 kHz LSE and retains time through ordinary MCU resets while
backup power remains present. If LSE startup fails, firmware falls back to LSI so recovery and
USB remain usable, but BLE startup is blocked. An unset RTC is seeded from firmware build time.
Use `j` to inspect it or enter `@YYYY-MM-DDTHH:MM:SS` followed by Enter to set it manually.

The timer is adjusted in one-minute steps on its screen. Scheduled weekday/weekend alarms,
five-minute snooze, quiet mode, vibration, and alarm overrides for the buttons are active.
The step counter uses the BMA400's hardware step engine, read on a slow cadence (no interrupt per
step) rather than periodic raw acceleration sampling. Resetting steps records a new sensor-counter baseline without resetting the
accelerometer itself.

The Settings card is a scrolling list (five rows visible, arrows mark more rows above/below, the
footer shows the position): Bluetooth, Quiet, LED ring, Vibration, Alert tone, Secure link,
Forget phone. Alert tone cycles NONE / CHIRP / DOUBLE / RISE / FALL / TRIPLE and plays the
new choice. Secure link shows `N/A` without working external flash. Forget phone needs a second
select (`OK?`) to confirm and shows `--` when no phone is bonded.

## Hang monitor

The watchdog resets a hung watch after 20 s but cannot say where it hung. A slow LPTIM1 tick
(`watch_watchdog.cpp`) notices when the watchdog has not been fed for 14 s and stores the
interrupted program counter and link register in RTC backup registers 16-18. The next boot prints
them:

```text
[WARN] Previous reset was caused by the watchdog: the firmware hung.
[WARN] It was stuck at PC=0x801E89E LR=0x801100B
```

Turn the address into a function with the ELF of the same build:

```text
arm-none-eabi-addr2line -f -C -e .pio/build/watch/firmware.elf 0x801E89E
```

"The hang monitor recorded no location" means the hang ran with interrupts masked, or the backup
domain was reset in between (starting the radio does that). LPTIM1 runs from PCLK1, so it stops in
Stop mode and never wakes the watch from it. Console `%` hangs on purpose to test both.

### I2C interrupt storms

The same tick also ends a failure of the STM32 HAL I2C driver. Its interrupt handler can return
without clearing the flag that raised it; the interrupt then fires again at once, for ever, and the
main code never runs again, so the Wire library's own 100 ms timeout cannot expire. On this board
it showed up as a hang in the boot-time sensor test, in anything from a third of boots to all of
them. An honest I2C interrupt lasts microseconds, so when two ticks in a row (131 ms apart) find
the CPU inside one, the tick masks that I2C's interrupts. The transfer in progress then times out
and `I2cBus::recoverIfStormed()` restarts the bus, from the main loop and between the sensor
start-ups. The sensor test prints `[WARN] I2C interrupt storms broken since boot: N` when it has
happened. The BME280 and MAX17048 libraries are also only started at an address that answers,
because they can wait for the device without a timeout.

The trigger was found afterwards: the Adafruit MAX17048 library sends the gauge's power-on-reset
command every time it starts, and the gauge resets instead of acknowledging it. That NACK in the
middle of a write set off the storm in 14 of 20 boots with a battery connected. `Max17048` now
reads and writes the gauge's registers itself and never resets it (20 of 20 boots clean), which
also lets the gauge keep its learned state of charge across firmware restarts. The storm breaker
stays as a safety net.

### Crash loops

A firmware that hangs on every boot never reaches its console, so it cannot be asked for USB DFU,
and with a battery connected there is no way to reset it by hand. `WatchWatchdog::begin()` counts
watchdog resets in a row in RTC backup register 14. On the third it requests the resident
bootloader's USB DFU mode instead of starting the application again. The count is cleared once the
firmware has stayed up for 60 s, and by any other kind of reset. The boot log prints it:
`[WARN] Watchdog resets in a row: 2 (USB DFU at 3)`. The check runs at the very start of `setup()`;
a crash before that is not caught. The resident bootloader has no timeout, so a watch that lands
there on battery stays in DFU until it is updated, reset or flat.

## External flash storage

`WatchFlash` (`watch_flash.cpp`) drives U4 by bit-banging SPI mode 0 on PA3 (CLK), PD3 (NCS),
PD4 (IO0/MOSI) and PD5 (IO1/MISO), with PD6 (WP#) and PD7 (HOLD#) held high as GPIO. It uses only
the single-line commands allowed by `EXTERNAL_FLASH.md`, every busy wait has a timeout and feeds
the watchdog, and the chip is in deep power-down except while an operation (or a firmware
transfer) holds it awake. A missing or dead chip reads as JEDEC `0xFFFFFF`/`0x000000` and every
operation then fails immediately.

`WatchConfigStore` (`watch_config.cpp`) keeps four records in `0x000000`-`0x007FFF`, each in a
pair of 4 KiB sectors (A/B). A save erases the sector that does not hold the newest valid copy,
writes the record there with the next sequence number and a CRC-32, and reads it back, so losing
power at any point leaves the previous copy intact. The exact layout is documented at the top of
`include/watch_config.h`.

| Record | Sectors | Contents | Written |
| --- | --- | --- | --- |
| Settings | `0x0000` / `0x1000` | vibration, quiet, alert tone, secure link | only when a value changes |
| Bond | `0x2000` / `0x3000` | bonded phone's identity address and link key | on pairing; erased by Forget phone or turning Secure link off |
| Steps | `0x4000` / `0x5000` | today's day number and count, six archived days | at midnight roll-over, at most hourly while the count changes, about a minute after a step reset, and before a deliberate reboot |
| Power | `0x6000` / `0x7000` | deep-sleep setting (console `0`/`1`/`2`) | only when changed from the console |

Without the flash (or after a write fails, until the next boot) everything keeps working from RAM
exactly as before. At boot the archived days are restored. Today's stored count is added back only
if its day equals the current local day; a count from an earlier day is archived as a finished
day. If the RTC has only its build-time seed (never set since the backup domain lost power) the
decision waits until the phone or the console sets the time, and nothing is rolled over or saved
until then, so the step figure is always "steps today".

## Bluetooth and companion app

BLE starts only when explicitly requested, preserving the lowest-power default and the USB/SWD
recovery path.
Press `r` in the serial console or select Bluetooth in Settings. The on/off choice is
stored in the external flash and restored at boot, except after a watchdog reset (a radio
start that hangs must not reset the watch in a loop; the next clean boot tries again). The watch advertises as
`E-Paper Watch`. STM32WB requires a compatible wireless coprocessor image; if startup fails,
install the matching `stm32wbxx_BLE_HCILayer_fw.bin` on CPU2/FUS.

The phone link implements protocol v2. [`PROTOCOL.md`](../PROTOCOL.md) at the repository root is
the contract shared with the Android companion; this section only summarises the watch side.

| Direction | Characteristic | Limit |
| --- | --- | ---: |
| Phone to watch | `7BD10001-6B10-4A21-9D6A-3A56B35C1000` | 160 bytes |
| Watch to phone | `7BD10002-6B10-4A21-9D6A-3A56B35C1000` | 160 bytes, sent as notifications of at most 20 |
| OTA data (write with response) | `7BD10003-6B10-4A21-9D6A-3A56B35C1000` | 4-byte offset + up to 240 bytes |

The custom service is `7BD10000-6B10-4A21-9D6A-3A56B35C1000`; the standard Battery Service is
also exposed. Messages are printable ASCII with `|`-separated fields; any other byte received is
replaced with `?`. The firmware version reported in the `HELLO` reply is `WATCH_FIRMWARE_VERSION`
in `include/watch_version.h`.

`WatchBle` (`watch_ble.cpp`) does all framing without heap allocation:

- Every phone write is `<seq>|TYPE|…`. It is parsed inside `BLE.poll()` into a fixed queue of
  eight `PhoneUpdate` slots and answered with `ACK|seq`, or `NAK|seq|PARSE`, `UNKNOWN`, or `FULL`.
  A repeat of the last accepted seq is re-ACKed but not applied twice; `HELLO` resets that memory
  and is answered with `ACK`, `HELLO|2|<fw>|<flags>`, then `STATUS`. Flags: `1` secure link on,
  `2` Bluetooth firmware update supported (`watch` build with usable flash), `4` external flash
  storage working.
- Watch-to-phone messages go through a sixteen-entry queue that is sent in order while the phone
  is subscribed and cleared on disconnect. ACK/NAK replies displace the oldest queued message
  if the queue is ever full. Fragmentation happens where the queue is drained
  (`WatchBle::drainOutbound`): a message of at most 20 bytes is one notification; a longer one is
  sent as `+`-prefixed chunks of 19 payload bytes ending with one `=`-prefixed chunk, and the next
  message never starts before the last chunk has gone.
- The link is also serviced during e-paper refreshes: the display's BUSY-wait hook feeds the
  watchdog and, about every 20 ms, calls `WatchBle::poll()` (receive, ACK, queue, send) and the
  actuator timing. Updates are still applied only by the main loop, and `poll()`/`flush()` refuse
  nested entry, so `BLE.poll()` is never re-entered and a render is never started from the hook.
- The main loop applies every queued update after each wake, then renders at most once.
  Notifications, events, and phone status never start an e-paper refresh by themselves; screens
  that show the changed data (Music, Settings, Messages after a clear) are redrawn.

What the watch does with the link:

| Feature | Watch behaviour |
| --- | --- |
| Time | The RTC holds local time. `TIME\|utc\|offset` sets it to `utc + offset` and stores the offset in RAM and RTC backup registers 12/13 (rewritten after `BLE.begin()` resets the backup domain). `@YYYY-MM-DDTHH:MM:SS` on the console still sets local time directly. |
| Notifications | Up to five, newest first; `NOTIFY` replaces an existing id. Inside Messages, previous/next page through them and select dismisses the shown one (`NOTIFY_DISMISS\|id`). With `alert` = 1 the watch gives one short vibration pulse (if Vibration is on) and plays the selected alert tone (unless Quiet is on or the tone is NONE); an alarm, find-my-watch or call alert keeps the actuators. No e-paper refresh is started. `TONE_PREVIEW\|n` plays tone n once. |
| Calls | `CALL\|RING` overlays a call screen and rings (vibration if enabled, plus buzzer) unless quiet mode is on. Select rejects (`CALL_REJECT`), back hides the screen, other buttons silence it. `CALL\|END` restores the previous screen. |
| Music | State none/playing/paused; the position advances locally while playing. Inside Music, previous/next/select send `MEDIA\|PREV`, `MEDIA\|NEXT`, `MEDIA\|PLAY_PAUSE`. |
| Upcoming | The clock face shows the next calendar event (or else the phone's next alarm) within 24 h on one line under the time. The Alarms card also shows the phone alarm. |
| Phone / link | The clock shows a Bluetooth icon while connected. Settings shows radio state, phone battery, charging, and Do Not Disturb. |
| Camera | Opening the Camera app sends `CAMERA\|OPEN` and the phone shows its camera screen. Select sends `CAMERA\|SHUTTER\|delay`, the top buttons pick the self-timer (off / 3 s / 10 s), back sends `CAMERA\|CLOSE`. The screen shows the phone's `CAMERA` state: opening, ready, the countdown, photos saved or the error. |
| Find | Opening the Find Phone app sends `FIND_PHONE\|1`; inside it select sends `\|0` or `\|1` again, and back stops the ringing as it leaves. The watch shows it as ringing for at most 2 min, as long as the phone rings. `FIND_WATCH` vibrates and buzzes for 10 s or until any button. |
| Settings | Vibration, quiet mode and alert tone are applied from the phone (`SETTINGS\|v\|q\|tone`, tone optional) and sent back in the same form when changed on the watch. They are stored in external flash. |
| Status / history | `STATUS\|battery\|mV\|steps\|charging` after `HELLO`, on request, when the battery percentage or the USB power state changes, and every 10 min. `charging` is 1 while USB power is present (the board has no charger status line), else 0. Steps roll over at local midnight into a history of six completed days; `REQUEST\|HISTORY` sends those plus today, oldest first, then `HISTORY_END`. The history survives resets when the external flash works. Both are suspended during a firmware transfer. |

### Secure link (optional, off by default)

With Settings -> Secure link off, the link is unencrypted exactly as before and notification text
travels in clear. Turning it on (needs working external flash; the radio restarts if it is
running) makes the watch refuse everything on an unencrypted link:

- Pairing is LE Secure Connections "Just Works" through STM32duinoBLE. It is accepted only while
  no phone is bonded; the key is then stored in the config store, and afterwards only a phone that
  can encrypt with that key is served. Forget phone (or `p`, or turning the setting off) erases
  the key; the bond must then be removed on the phone as well.
- The requirement is enforced by `WatchBle`, not by the library's encrypted-characteristic
  permission, because that code path in STM32duinoBLE 1.4.2 copies a write of up to 244 bytes
  into a 64-byte buffer. On an unencrypted link the watch therefore answers any write only with an
  unacknowledged `HELLO|2|<fw>|<flags>` (flag `1` set) at most once a second, applies nothing,
  accepts no OTA data and sends nothing else. The phone gets no ATT "insufficient encryption"
  error and has to start bonding itself when it sees flag `1`.

### Firmware update over Bluetooth

Offered (`HELLO` flag `2`) only by the `watch` build with a usable external flash; `watch_direct`
answers `OTA_STATE|UNSUPPORTED`. `WatchOta` (`watch_ota.cpp`) implements the watch side:

1. `OTA_BEGIN|size|crc32|version` shows a confirmation screen. Select accepts, back declines,
   60 s without an answer declines. `OTA_STATE|BUSY` if an update is already in progress or an
   alarm/call is active; `ERROR|SIZE` if the size is outside 8..761856.
2. On accept the header sector (`0x100000`) is erased and magic/format/size/CRC/header-CRC are
   written, then `OTA_STATE|ACCEPT|240|1`. The trailing `1` says full-size packets get
   through (the radio-core buffer fix in `tools/patch_libraries.py`); without it the phone
   sends at most 96 bytes a chunk, which earlier firmware can receive.
3. Each write to the OTA data characteristic must carry the next expected offset; anything else
   is ignored and answered with `OTA_STATE|PROGRESS|<expected>`. Each 4 KiB sector of the staging
   area is erased just before the first byte goes into it, and the data is programmed before the
   write is acknowledged. `PROGRESS` is also sent every 16 KiB and at the end. 30 s without an
   accepted chunk ends the transfer with `ERROR|TIMEOUT`.
4. `OTA_END`: the staged copy is read back and its size, CRC-32 and vector table checked, the
   header is re-read, and only then the `ready` word is programmed and verified. `OTA_STATE|READY`
   is queued, an "INSTALLING UPDATE / ABOUT 30 S / DO NOT UNPLUG" screen is drawn (the bootloader
   draws nothing while it installs), steps are saved, the flash is left awake and idle, and the
   watch resets through the normal boot path at least 2 s after `READY`.
5. `OTA_ABORT`, a disconnect, a timeout or any flash error discards the transfer by erasing the
   header sector; the running firmware is never touched. The e-paper is refreshed only for the
   prompt, 0/25/50/75 %, "verifying" and the final screen, and buttons are ignored during a
   transfer.
6. At boot the header is read. A bootloader `result` is reported once after the next `HELLO`
   (`OTA_STATE|INSTALLED|1` or `FAILED|<code>`) and the header sector is then erased. A header
   that is `ready` but has no result (bootloader without update support) is erased immediately and
   reported as `FAILED|0`; a header without `ready` (interrupted transfer) is just erased.

See the [Android companion documentation](../companion/README.md) for setup.

Before running `d`, `w`, or `k`, verify that display bus-select jumper JP1 bridges BS to GND
(pins 1-2) for four-wire SPI. If the reset probe passes but power-on or refresh times out, inspect
the GDR/RESE booster circuit and generated rails. The current schematic uses a 2.2 uH L1 while the
ET011TJ1 reference circuit in `spooky.pdf` specifies 10 uH, so L1 is the first component value to
verify if the controller logic works but the booster does not.

## First hardware test sequence

1. Install the combined factory image once, then use `tools/usb_dfu_update.py` for later application builds.
2. Confirm the automatic peripheral test completes and `j` reports a plausible retained time.
3. Press `u` once. Use the right-hand buttons (PCB 2/3) to browse the cards; bottom-left
   (PCB 4) returns to the clock.
4. Browse to Timer, press top-left (PCB 1) to enter it, adjust with the right-hand buttons, then
   press top-left to start. Bottom-left backs out to the Timer card; a second press returns to the clock. Verify vibration/buzzer at zero,
   button 1 snoozes, and button 4 dismisses.
5. Set a near-future alarm with `!HH:MM`, then test the same alarm controls.
6. Press `r`; wait for `BLE advertising as "E-Paper Watch"`.
7. Install/open the companion, grant nearby-device and notification permissions, and Connect.
   Before that, check the boot log (or press `q`) for `External flash JEDEC ID 0x0B4016` and a
   `Config store: ok` line; change a setting, reset, and confirm it is restored.
8. Watch serial for `Phone HELLO, protocol 2` and the UTC offset from the time sync. Then test
   live Android notifications (page and dismiss), an incoming call, media controls,
   find phone/watch, and the quiet/vibration settings sync.

Then work through "Hardware checks, in order" in the power section before relying on deep sleep.
