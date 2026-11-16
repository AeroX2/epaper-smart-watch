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
STM32CubeProgrammer to erase, program, verify, and start the application. The
USB path involves no external-flash staging (that is only used by the Bluetooth
update described below).
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

`q` re-probes the external flash without writing to it and prints its JEDEC ID (expected
`0x0B4016`), status register, the config-store state (which sector copy and sequence number each
record uses, and how many writes this boot), the Bluetooth-update state, and the secure-link /
bond / encryption state. The same flash line is printed at boot. `t` plays the next notification
alert tone (1-5) regardless of settings. `p` forgets the bonded phone (erases the stored link key).

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

The Settings card is a scrolling list (five rows visible, arrows mark more rows above/below, the
footer shows the position): Bluetooth, Quiet, LED ring, Vibration, Alert tone, Find phone, Secure
link, Forget phone. Alert tone cycles NONE / CHIRP / DOUBLE / RISE / FALL / TRIPLE and plays the
new choice. Secure link shows `N/A` without working external flash. Forget phone needs a second
select (`OK?`) to confirm and shows `--` when no phone is bonded.

## External flash storage

`WatchFlash` (`watch_flash.cpp`) drives U4 by bit-banging SPI mode 0 on PA3 (CLK), PD3 (NCS),
PD4 (IO0/MOSI) and PD5 (IO1/MISO), with PD6 (WP#) and PD7 (HOLD#) held high as GPIO. It uses only
the single-line commands allowed by `EXTERNAL_FLASH.md`, every busy wait has a timeout and feeds
the watchdog, and the chip is in deep power-down except while an operation (or a firmware
transfer) holds it awake. A missing or dead chip reads as JEDEC `0xFFFFFF`/`0x000000` and every
operation then fails immediately.

`WatchConfigStore` (`watch_config.cpp`) keeps three records in `0x000000`-`0x005FFF`, each in a
pair of 4 KiB sectors (A/B). A save erases the sector that does not hold the newest valid copy,
writes the record there with the next sequence number and a CRC-32, and reads it back, so losing
power at any point leaves the previous copy intact. The exact layout is documented at the top of
`include/watch_config.h`.

| Record | Sectors | Contents | Written |
| --- | --- | --- | --- |
| Settings | `0x0000` / `0x1000` | vibration, quiet, alert tone, secure link | only when a value changes |
| Bond | `0x2000` / `0x3000` | bonded phone's identity address and link key | on pairing; erased by Forget phone or turning Secure link off |
| Steps | `0x4000` / `0x5000` | today's day number and count, six archived days | at midnight roll-over, at most hourly while the count changes, about a minute after a step reset, and before a deliberate reboot |

Without the flash (or after a write fails, until the next boot) everything keeps working from RAM
exactly as before. At boot the archived days are restored. Today's stored count is added back only
if its day equals the current local day; a count from an earlier day is archived as a finished
day. If the RTC has only its build-time seed (never set since the backup domain lost power) the
decision waits until the phone or the console sets the time, and nothing is rolled over or saved
until then, so the step figure is always "steps today".

## Bluetooth and companion app

BLE starts only when explicitly requested, preserving the lowest-power default and the USB/SWD
recovery path.
Press `r` in the serial console or select Bluetooth in Settings. The watch advertises as
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
  that show the changed data (Weather, Music, Settings, Messages after a clear) are redrawn.

What the watch does with the link:

| Feature | Watch behaviour |
| --- | --- |
| Time | The RTC holds local time. `TIME\|utc\|offset` sets it to `utc + offset` and stores the offset in RAM and RTC backup registers 12/13 (rewritten after `BLE.begin()` resets the backup domain). `@YYYY-MM-DDTHH:MM:SS` on the console still sets local time directly. |
| Notifications | Up to five, newest first; `NOTIFY` replaces an existing id. Inside Messages, previous/next page through them and select dismisses the shown one (`NOTIFY_DISMISS\|id`). With `alert` = 1 the watch gives one short vibration pulse (if Vibration is on) and plays the selected alert tone (unless Quiet is on or the tone is NONE); an alarm, find-my-watch or call alert keeps the actuators. No e-paper refresh is started. `TONE_PREVIEW\|n` plays tone n once. |
| Calls | `CALL\|RING` overlays a call screen and rings (vibration if enabled, plus buzzer) unless quiet mode is on. Select rejects (`CALL_REJECT`), back hides the screen, other buttons silence it. `CALL\|END` restores the previous screen. |
| Music | State none/playing/paused; the position advances locally while playing. Inside Music, previous/next/select send `MEDIA\|PREV`, `MEDIA\|NEXT`, `MEDIA\|PLAY_PAUSE`. |
| Upcoming | The clock face shows the next calendar event (or else the phone's next alarm) within 24 h on one line under the time. The Alarms card also shows the phone alarm. |
| Phone / link | The clock shows a Bluetooth icon while connected. Settings shows radio state, phone battery, charging, and Do Not Disturb. |
| Find | Settings → Find phone sends `FIND_PHONE\|1`, and `\|0` on the next press. `FIND_WATCH` vibrates and buzzes for 10 s or until any button. |
| Settings | Vibration, quiet mode and alert tone are applied from the phone (`SETTINGS\|v\|q\|tone`, tone optional) and sent back in the same form when changed on the watch. They are stored in external flash. |
| Status / history | `STATUS\|battery\|mV\|steps\|-1` after `HELLO`, on request, when the battery percentage changes, and every 10 min. Steps roll over at local midnight into a history of six completed days; `REQUEST\|HISTORY` sends those plus today, oldest first, then `HISTORY_END`. The history survives resets when the external flash works. Both are suspended during a firmware transfer. |
| Sleep as Android | `SLEEP_START`/`STOP`/`PAUSE`/`ALARM`/`ALARM_STOP`/`NOTIFY`/`HINT`; the watch sends `SLEEP_DATA\|max-delta-mg` every ten seconds while tracking, plus `SLEEP_SNOOZE`/`SLEEP_DISMISS` from the alarm. |

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
   written, then `OTA_STATE|ACCEPT|240`.
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
3. Press `u` once. Use the top buttons (PCB 2/3) to browse the eight cards; bottom-left (PCB 1)
   returns to the clock.
4. Browse to Timer, press bottom-right (PCB 4) to enter it, adjust with the top buttons, then press
   bottom-right to start. Bottom-left backs out to the Timer card; a second press returns to the clock. Verify vibration/buzzer at zero,
   button 4 snoozes, and button 1 dismisses.
5. Set a near-future alarm with `!HH:MM`, then test the same alarm controls.
6. Press `r`; wait for `BLE advertising as "E-Paper Watch"`.
7. Install/open the companion, grant nearby-device and notification permissions, and Connect.
   Before that, check the boot log (or press `q`) for `External flash JEDEC ID 0x0B4016` and a
   `Config store: ok` line; change a setting, reset, and confirm it is restored.
8. Watch serial for `Phone HELLO, protocol 2` and the UTC offset from the time sync. Then test
   live Android notifications (page and dismiss), an incoming call, weather, media controls,
   find phone/watch, and the quiet/vibration settings sync.
9. Start the companion's test tracking and watch serial for a movement sample every ten seconds.

Power behavior should still be measured on the assembled watch, particularly with BLE connected
and during Sleep as Android tracking. Those modes naturally wake more often than the idle clock.
