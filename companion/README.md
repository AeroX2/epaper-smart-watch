# E-Paper Watch Android companion

This dependency-light Kotlin app is the test companion for the STM32WB watch firmware. It provides:

- BLE scan, reconnect, foreground connection status, and phone-to-watch time sync
- opt-in Android notification forwarding
- active media metadata plus previous/play-pause/next control from the watch
- manual weather-card values, so no API key or cloud account is required for bring-up
- a movement-only Sleep as Android wearable-intent bridge

## Build and install

Open `companion` in Android Studio, or build from PowerShell:

```text
cd companion
.\gradlew.bat assembleDebug
adb install -r app\build\outputs\apk\debug\app-debug.apk
```

The project targets Android 14 (API 34), has a minimum of Android 8, and uses only Android platform
APIs plus the Kotlin runtime. The debug APK is generated at
`app/build/outputs/apk/debug/app-debug.apk`.

## Connect

1. Flash and boot the watch.
2. Open its USB serial console and press `r`. Wait for `BLE advertising as "E-Paper Watch"`.
3. Open the companion and grant Nearby devices and notification permissions.
4. Press **Connect**.
5. Press **Enable notification access** and enable E-Paper Watch if notifications/media are wanted.

The foreground `WatchLinkService` retains the connection and automatically reconnects after a
link loss. It deliberately does not start the watch radio: radio startup remains an explicit
watch-side action while STM32WB recovery behaviour is being validated.

## Feature checks

- **Sync phone time** sends current Unix time to the STM32 RTC.
- **Send a test notification** checks the GATT path without needing notification access.
- **Weather card** sends entered current/high/low/condition values.
- With notification access enabled, new Android notifications are reduced to app/title/body and
  forwarded. The watch does not retain a phone notification history.
- With an active Android media session, the app publishes title, artist, duration, position, and
  state. Watch media buttons call the session's transport controls.
- **Test tracking** enables ten-second BMA400 movement summaries. This button tests watch sampling;
  a real Sleep session controls it through Sleep's intents.

## Sleep as Android

The bridge implements connection confirmation, tracking start/stop, pause/suspend, requested batch
size, alarm start/stop/update, generic notifications, hints, movement upload, snooze, and dismiss.
Movement is calculated on-watch as the largest three-axis acceleration change in each ten-second
interval, converted by Android from milligravity to m/s², batched, and sent as `MAX_RAW_DATA`.

Sleep as Android requires third-party packages using its wearable API to be explicitly whitelisted
on modern Android. The provisional package is `com.james.epaperwatch`; contact Urbandroid before
expecting a production Sleep install to send commands to it. If the final package name changes,
request whitelisting for the final name instead.

This hardware has no heart-rate, HRV, or SpO2 sensor, so the companion never fabricates those data.

## Current security and product limitations

This is an end-to-end feature prototype, not the final secure release. The custom GATT messages are
human-readable and are not yet application-encrypted, bonded, versioned, acknowledged, or stored
persistently. Do not expose sensitive notification contents during public radio testing. Before a
daily-use release, add authenticated bonding/encryption, privacy filters, persistent settings,
Android companion-device association, and the firmware low-power state machine.
