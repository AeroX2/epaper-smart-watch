# E-paper Smart Watch Product and Firmware Plan

## Product direction

The watch should be a low-power, glanceable companion rather than a miniature
phone. Its strengths are an always-visible e-paper face, physical controls,
long idle time, local alarms, useful sensors, and a private Bluetooth link to an
Android phone.

The first usable product should do five things reliably:

1. Keep and display time even when the phone is disconnected.
2. Show selected Android notifications.
3. Provide local alarms, timers, battery status, and basic sensor views.
4. Control phone media and find the paired phone.
5. Sleep for long periods and wake from the RTC, buttons, accelerometer, fuel
   gauge alert, or Bluetooth events.

This is a single-purpose embedded UI. Third-party executable apps and a general
app store are out of scope. Additional features should be compiled-in screens
or phone-provided glance cards.

## Hardware we can build around

| Hardware | Intended product use |
| --- | --- |
| STM32WB5MMG | Main application, STM32 RTC, Bluetooth LE, security, and low-power control |
| ET011TJ1 240 x 240 e-paper | Always-visible black-and-white UI |
| 4 MiB XT25F32 external NOR flash | Fonts, icons, watch-face assets, circular logs, and future update staging |
| BMA400 accelerometer | Wake gestures, steps/activity, orientation, and Sleep as Android movement data |
| BME280 | Pressure, humidity, and approximate local temperature |
| MAX17048 | Battery voltage, state of charge, charge/discharge trend, and low-battery alert |
| Four buttons | Deterministic navigation and alarm control |
| Three capacitive inputs | Optional shortcuts or gestures after reliable calibration |
| Vibration motor and buzzer | Silent and audible alarms and feedback |
| Display LED ring | Explicit user-controlled lighting and alarm/status effects |

Important limitations:

- There is no magnetometer, so this board cannot measure compass heading. The
  accelerometer can show tilt or orientation, but calling that a compass would
  be misleading. A connected-phone heading screen is possible but low priority.
- There is no GPS. Location, route, weather forecast, sunrise, and navigation
  data must come from the phone.
- There is no heart-rate or SpO2 sensor. Sleep integration can provide movement
  data, but this is not a medical or sleep-stage measurement device.
- BME280 temperature is affected by skin, enclosure, MCU, charger, and display
  heat. It must be labelled as local/device temperature, not body temperature.
- E-paper is not suited to seconds, animations, scrolling, or constant redraws.

## Firmware architecture

Keep the working peripheral code, but move from the current serial-test program
to small layers with explicit ownership:

```text
Board drivers
  display, buttons/touch, RTC, BMA400, BME280, MAX17048, NOR flash,
  vibration, buzzer, LED ring
        |
System services
  event queue, power manager, time/alarm service, settings, storage,
  display scheduler, BLE transport
        |
Applications
  clock face, notification viewer, alarms/timers, activity, environment,
  music, weather, settings, diagnostics
        |
Android companion
  pairing, time/weather sync, notifications, media, Sleep as Android
```

The main firmware should be event-driven. It should not poll every peripheral
continuously. Drivers publish events, services update state, and the display
scheduler coalesces changes into the fewest possible refreshes.

### Core data model

Use small fixed-size records and bounded queues. Avoid heap allocation in the
steady state.

- `SystemState`: battery, connection, time validity, power mode, active screen.
- `UiEvent`: button/touch action, timer expiry, notification, sensor alert.
- `NotificationSummary`: ID, source, title, short body, timestamp, flags.
- `Alarm`: local time, repeat mask, enabled flag, label, sound/vibration mode.
- `SensorSnapshot`: timestamp, steps, movement, pressure, humidity, temperature.
- `Settings`: watch face, units, button mapping, notification filters, brightness.

Persist schema versions and CRCs so future firmware can migrate or reject old
records safely.

## Display and font handling

### Font strategy

Do not render TTF/OTF fonts on the STM32. Convert fonts on the development
computer into watch-native bitmap assets:

1. Select an OFL-licensed source font.
2. Rasterize only the required glyphs at the exact pixel sizes used by the UI.
3. Convert to a compact row-packed black-and-white format.
4. Generate a glyph table containing code point, dimensions, bearings, advance,
   and flash offset.
5. Pack the glyph bitmaps and table into the external-flash asset image.
6. Keep a tiny emergency font in internal MCU flash so recovery and error
   screens still work if external flash fails.

The main clock font only needs digits `0` through `9`, colon, and optionally
`AM`/`PM`. A deliberately small glyph set allows a large, carefully hinted face
without wasting memory. Digits must be tabular so the time does not jump
horizontally as it changes.

Suggested visual starting points:

- Large clock: DSEG7 Modern Bold for a classic digital look, or IBM Plex
  Mono/Space Grotesk tabular digits for a less calculator-like face.
- UI text: Atkinson Hyperlegible or Inter, initially at approximately 14, 18,
  and 24 px.
- Icons: purpose-drawn one-bit symbols at their final sizes rather than a large
  generic icon font.

The exact face is a visual decision, but the firmware-facing format stays the
same. That allows font packs and watch faces to change without rewriting the
renderer.

### Rendering strategy

- Start with a simple retained framebuffer and custom drawing primitives:
  pixels, lines, rectangles, monochrome bitmaps, and bitmap text.
- Render black and white first. Only introduce grayscale if repeatable panel
  testing proves that the display and LUT support it safely.
- Do not adopt a large GUI framework until partial-update behaviour and memory
  costs are characterised. The initial screen stack is simple enough for a
  purpose-built renderer.
- The clock does not show seconds.
- Add a display scheduler that tracks dirty regions, merges changes, and owns
  all refreshes.
- If reliable partial refresh is achieved, update only changed clock digits and
  periodically perform a full cleaning refresh to limit ghosting.
- If only full refresh is reliable, refresh when the user wakes the watch, when
  an important notification arrives, or at a configurable coarse interval. Do
  not visibly flash the panel every minute while the user is not looking.
- After every refresh, turn off the display booster and return the controller
  to the known safe standby/reset state already established by bring-up.

The current use of PB4 as both button 4 and the display SPI object's dummy MISO
must be removed or formally contained before the UI relies on that button.

## External flash plan

The fitted 32-Mbit part provides 4 MiB. Use fixed aligned partitions at first,
not a general-purpose filesystem:

| Region | Initial size | Purpose |
| --- | ---: | --- |
| Metadata and settings journal | 64 KiB | Redundant partition table, settings records, CRCs, and crash markers |
| Assets | 1 MiB | Fonts, icons, watch faces, boot/recovery graphics |
| Circular data log | 1.9375 MiB | Activity, sleep movement summaries, battery, and environmental history |
| Update/recovery staging | 1 MiB | Future firmware or asset-pack download and verification |

The exact split can change before the on-flash format is declared stable.

Before storing anything, expand the current read-only QSPI probe into a
destructive test confined to an explicitly reserved scratch sector:

- identify the fitted XT25F32 variant and its status-register/QE behaviour;
- read, erase, program, verify, power-cycle, and re-read;
- implement write-in-progress polling, timeouts, deep power-down, and recovery;
- never erase outside the declared scratch sector during development.

Storage rules:

- Use 4 KiB sector alignment and append-only records.
- Put sequence numbers and CRCs on every journal/log record.
- Use A/B metadata sectors so power loss cannot destroy the partition table.
- Erase circular-log sectors ahead of use and spread wear across the region.
- Treat asset packs as immutable, versioned blobs with a manifest and whole-pack
  hash.
- Do not store notification bodies indefinitely. Keep them in RAM or a short,
  user-configurable queue.
- Reserve update staging, but postpone OTA until signed-image verification and
  rollback are designed. An unauthenticated OTA path is worse than no OTA.

Useful flash-backed features beyond fonts include:

- several watch faces and icon themes;
- recent step, sleep-movement, battery, and pressure trends;
- a short offline notification queue;
- crash/reset diagnostics;
- phone-sent checklist or note cards;
- language packs;
- a future signed firmware/asset update image.

## Navigation and interaction

Map physical locations to abstract actions after confirming which numbered
button is where on the enclosure:

| Action | Default behaviour |
| --- | --- |
| Back | Return to previous screen; long press returns home |
| Up | Previous item/card; on the clock opens notifications |
| Select | Open/confirm; on the clock opens the launcher |
| Down | Next item/card; on the clock opens quick settings |

Interaction rules:

- Short press and long press are sufficient for the first version.
- Alarm ringing overrides normal navigation: one large, consistent action
  snoozes; a deliberate long press dismisses.
- Every action should produce visual feedback; vibration feedback is optional
  and configurable.
- Capacitive inputs remain disabled in the product UI until baseline tracking,
  moisture behaviour, false-touch rejection, and sleep current are measured.
- Avoid hidden multi-button chords except for recovery/debug functions.

The primary interface should be:

1. Clock face with battery, connection, next alarm, and one or two complications.
2. Up/down “glance cards” for notification, weather, agenda, and activity.
3. Select opens a short launcher for less frequent tools.

## Applications

### Tier 1: product core

| App/screen | Behaviour |
| --- | --- |
| Digital clock | Large HH:MM, date/day, battery, BLE state, next alarm, optional complication |
| Notifications | Filtered Android notification cards with source, title, body preview, age, dismiss/archive |
| Alarms | Multiple offline alarms, repeat days, snooze, vibration/buzzer selection |
| Timers and stopwatch | RTC-backed timer and simple stopwatch; no high-rate display updates |
| Quick settings | Connection, quiet mode, LED ring, vibration, units, battery details |
| Diagnostics | Sensor status, reset cause, firmware version, BLE state, and safe hardware tests |

### Tier 2: high-value companion features

| App/screen | Behaviour |
| --- | --- |
| Weather | Current conditions and compact forecast supplied by Android; clearly separate phone forecast from local BME readings |
| Music | Track/artist plus play/pause, previous, and next through the Android media session |
| Find phone | Make the companion app ring/vibrate, with an explicit stop action |
| Agenda | Next few calendar events sent by the companion app; no calendar account credentials on the watch |
| Activity | Steps, active minutes, and a small history chart using BMA400 data |
| Environment | Pressure, humidity, device/local temperature, and trends |
| Sleep | Start/stop status, alarm state, and movement upload for Sleep as Android |

### Tier 3: good fits after the core is stable

- Pomodoro/focus timer with silent vibration.
- Breathing pacer using sparse screen changes and vibration cues.
- Phone camera shutter.
- Phone-provided turn-by-turn instruction cards.
- Sunrise/sunset and tide cards based on phone location.
- Pressure trend and approximate elevation change after phone calibration.
- A pinned checklist, shopping list, QR code, or short note synced from Android.
- Morning brief shown after alarm dismissal: weather, battery, and first event.
- “Do not disturb until…” and notification triage shortcuts.
- Configurable watch-face complications supplied by the phone.

### Ideas to avoid or defer

- Standalone compass: impossible without a magnetometer.
- Standalone weather forecast: impossible without internet/location data.
- Smooth second hand, animations, or games: poor match for e-paper refresh.
- On-watch maps: storage, interaction, and display-refresh cost exceed the value.
- Medical claims or inferred sleep stages: unsupported by the available sensors.
- Arbitrary downloaded executable apps: large security and stability cost.
- TOTP authenticator: technically possible, but defer until secure provisioning,
  secret storage, backup, and threat modelling are complete.

## RTC and time

Use the STM32 RTC and the module's fitted 32.768 kHz crystal:

- Keep the RTC in UTC.
- Sync UTC, timezone, and daylight-saving information from Android after pairing
  and periodically thereafter.
- Store time-valid and last-sync metadata; visibly indicate invalid time after a
  fully flat/disconnected battery.
- Use RTC wake-up/alarm facilities for minute boundaries, alarms, timers, and
  maintenance jobs.
- Compute display time from UTC plus the current phone-provided timezone rules.
- Local alarms must remain functional without Bluetooth.
- Measure RTC drift over several days and support a calibration value.

## Bluetooth and Android companion

### Firmware side

The first Bluetooth milestone is a risk-reduction spike. Confirm that the
current STM32 Arduino/PlatformIO environment can reliably run the STM32WB
wireless stack on the custom STM32WB5MMG board, including reconnect and low
power. STM32duino documents STM32WB support, but the radio stack, FUS version,
and memory layout must be validated on this exact module. If that path remains
fragile, move the product firmware to STM32CubeWB rather than building the rest
of the watch around an unstable BLE layer.

Expose standard services where they genuinely fit:

- Device Information Service
- Battery Service
- Current Time Service

Use one versioned custom GATT service for:

- phone-to-watch events: notification, weather, agenda, time/timezone, media
  metadata, alarm command, and configuration;
- watch-to-phone events: button action, media command, find-phone request,
  movement batch, battery/sensor status, and diagnostics;
- chunked, acknowledged bulk transfer for future asset packs or updates.

Use bounded binary messages with version, type, sequence, payload length, and
CRC. Pair and bond, require encryption for personal data and control messages,
and expose no notification contents while unpaired.

### Android side

Build a small Kotlin companion app with these responsibilities:

- pair through Android's Companion Device Manager;
- maintain the long-lived BLE relationship through `CompanionDeviceService`;
- sync time, timezone, preferences, weather, and agenda;
- use `NotificationListenerService` for opt-in, per-app filtered notifications;
- use Android media-session controls for music commands and metadata;
- provide firmware/log diagnostics and future asset-pack management;
- relay Sleep as Android intents and movement batches.

The app should request permissions only when the related feature is enabled.
Notification text and calendar data should be reduced to the minimum sent to the
watch. No cloud service is required for the basic product.

## Sleep as Android integration

Sleep as Android officially supports third-party wearables through a separate
Android add-on that communicates with the wearable and relays explicit Android
intents. The main companion app can serve as that add-on.

Required flow:

1. The phone sends connection/start/stop/pause and alarm commands to the
   companion app.
2. The companion translates them to the custom BLE service.
3. During tracking, the BMA400 samples motion while the main MCU stays in a
   low-power tracking state.
4. The watch calculates acceleration change across all axes, finds the maximum
   for each 10-second interval, and batches the summaries.
5. The companion sends `com.urbandroid.sleep.watch.DATA_UPDATE` with
   `MAX_RAW_DATA` in m/s².
6. Alarm, snooze, dismiss, pause, and resume commands work in both directions.

Sleep as Android states that packages using its wearable API on modern Android
must be explicitly whitelisted. Contact Urbandroid after choosing the final
Android package name, before treating the integration as guaranteed.

Start with movement only. The hardware has no reliable heart-rate, HRV, or SpO2
source, so those fields must not be fabricated.

References:

- [Sleep as Android wearable integration API](https://sleep.urbandroid.org/docs/devs/wearable_api.html)
- [Sleep as Android intent API](https://sleep.urbandroid.org/docs/devs/intent_api.html)
- [Android companion-device pairing](https://developer.android.com/develop/connectivity/bluetooth/companion-device-pairing)
- [Android BLE background communication](https://developer.android.com/develop/connectivity/bluetooth/ble/background)
- [Android notification listener](https://developer.android.com/reference/android/service/notification/NotificationListenerService)

## Power architecture

Battery life is a feature, so it needs a measured state machine rather than
scattered delays:

| State | Expected behaviour |
| --- | --- |
| Active | UI interaction, brief sensor work, display refresh as needed |
| Connected idle | BLE connection maintained at a relaxed interval; MCU mostly sleeping |
| Disconnected idle | Slow advertising or periodic advertising windows; RTC/buttons/alerts wake the MCU |
| Sleep tracking | BMA400/FIFO and timed aggregation active; display and unnecessary sensors off |
| Alarm | Vibration/buzzer/display driven by an explicit pattern |
| Charging | Show status on demand; permit diagnostics and firmware transfer |
| Critical battery | Disable LED ring, buzzer, radio-heavy work, and unnecessary refreshes |

Power rules:

- Use BMA400 interrupts/FIFO instead of continuous high-rate I2C polling.
- Run BME280 in forced mode only when a reading is requested.
- Put external NOR flash into deep power-down when unused.
- Keep the display booster and LED ring off except during explicit operations.
- Use RTC, button, accelerometer, fuel-gauge, and BLE wake sources.
- Measure current for every state before claiming battery life.
- Log reset cause, low-battery transitions, and unexpected wake reasons.

## Delivery roadmap

### Phase 0 — Stabilise the hardware foundation

- Preserve the working peripheral bring-up command firmware.
- Prove at least 100 alternating black/white/diagnostic refreshes without a
  stuck panel or unsafe booster state.
- Add a real framebuffer upload path and bitmap drawing smoke test.
- Prove RTC set, read, alarm, reset retention, and minute wake.
- Perform scratch-sector NOR erase/program/read/power-cycle testing.
- Run a minimal BLE GATT server on the exact board and verify reconnect,
  bonding, range, and current consumption.
- Decide whether Arduino/PlatformIO remains viable or STM32CubeWB is required.

Exit criterion: every load-bearing subsystem has a repeatable automated or
console test and a documented recovery path.

### Phase 1 — Wearable OS foundation

- Introduce driver/service/application boundaries and a central event queue.
- Implement debounced short/long button actions.
- Implement time, alarm, settings, power, storage, and display-scheduler
  services.
- Add the monochrome framebuffer, primitives, bitmap-font format, host asset
  converter, and emergency internal font.
- Add home, launcher, back stack, dialogs, and status-bar components.

Exit criterion: the watch boots to a clock, navigates placeholder screens, keeps
time, sleeps, wakes, and survives resets without corrupting settings.

### Phase 2 — Offline MVP

- Complete digital clock face and selectable font/face.
- Add battery, charging, alarm, timer, stopwatch, environment, activity, quick
  settings, and diagnostics screens.
- Add local recurring alarms with snooze/dismiss and vibration/buzzer patterns.
- Add BMA400 wake gesture, step counter, and activity summaries.
- Measure and tune active, idle, and tracking current.

Exit criterion: the watch is useful for a full day without a phone.

### Phase 3 — Android companion

- Implement secure companion pairing and reconnect.
- Add time/timezone and settings synchronisation.
- Add filtered notifications and notification history.
- Add find-phone, media control, weather, and agenda cards.
- Add a connection/permissions diagnostic screen on Android.

Exit criterion: everyday phone features recover automatically after either
device reboots or moves out of range.

### Phase 4 — Sleep and history

- Implement low-power BMA400 sampling, 10-second aggregation, batching, and
  circular logs.
- Implement the Sleep as Android wearable-intent bridge.
- Obtain package whitelisting from Urbandroid and run its wearable sensor test.
- Add alarm/snooze/dismiss round trips and interrupted-connection recovery.
- Add on-watch activity, pressure, battery, and sleep-movement trend views.

Exit criterion: an overnight session completes without data loss or excessive
battery drain and appears correctly in Sleep as Android.

### Phase 5 — Polish and safe updates

- Characterise and, if reliable, add partial refresh plus ghosting management.
- Add asset packs, alternate faces, notes/checklists, and morning brief.
- Design authenticated firmware update, rollback, and recovery.
- Add accessibility options, notification privacy controls, and localisation.
- Run multi-day battery, reconnect, flash-wear, and power-loss testing.

## Near-term implementation order

The next code work should be:

1. Add RTC bring-up and a minute-alarm console test.
2. Add a scratch-sector write/erase/verify test for the exact XT25F32.
3. Add a framebuffer API and draw the first real clock face with a compiled-in
   digit font.
4. Refactor button input and resolve the PB4/display-SPI dummy-MISO conflict.
5. Add the event queue, screen stack, and display scheduler.
6. Run the BLE feasibility spike before building the Android app.

This sequence answers the largest architectural unknowns early and produces a
visible, useful clock before the phone integration expands the project.
