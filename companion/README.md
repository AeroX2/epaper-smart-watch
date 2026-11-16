# E-Paper Watch Android companion

Kotlin companion app for the STM32WB e-paper watch. It implements the phone side of
[`PROTOCOL.md`](../PROTOCOL.md) (protocol v2). The UI and the watch link use only Android platform
APIs (plain `Activity`/`View`, no Compose). The one AndroidX dependency is the Health Connect client
(with kotlinx-coroutines for its suspend API), used only for the optional step sync.

## Build, test and install

```text
cd companion
.\gradlew.bat assembleDebug lintDebug testDebugUnitTest
adb install -r app\build\outputs\apk\debug\app-debug.apk
```

Gradle 8.9 cannot run on JDK 25. If `java` on your PATH is JDK 25, point `JAVA_HOME` at a JDK 17 or 21
for the build, for example `$env:JAVA_HOME = "C:\Program Files\Microsoft\jdk-21.0.3.9-hotspot"`.

Targets Android 14 (API 34), minimum Android 8 (API 26). `android.useAndroidX=true` is set because of
the Health Connect dependency.

Dependencies:

- `androidx.health.connect:connect-client:1.1.0-alpha08`. This is the newest release that still
  builds with compileSdk 34 and AGP 8.7.3: 1.1.0-alpha09 … beta need compileSdk 35, and the stable
  1.1.0 (and its release candidates) need compileSdk 36 and AGP 8.9.1. Move to the stable release
  when the toolchain is raised; `health/StepSync.kt` is the only file that uses it.
- `org.jetbrains.kotlinx:kotlinx-coroutines-android:1.8.1`.
- `junit:junit:4.13.2` (tests only).

JVM unit tests (`app/src/test`) cover the one-time-code redaction, watch → phone fragment
reassembly, firmware image validation and the Health Connect day window.

## Pairing and connection

1. Flash and boot the watch so it advertises the watch service (`7BD10000-…`).
2. Open the app and press **Pair watch**. Grant *Nearby devices* (Android 12+) and notifications
   (Android 13+) when asked.
3. Pick the watch in the system Companion Device chooser. The app stores its address.

The link service (`WatchLinkService`, foreground type `connectedDevice`) then reconnects with
`connectGatt(autoConnect = true)` to that address. The watch advertises a fixed public address, so
on Android 13+ the device is obtained with `getRemoteLeDevice(mac, ADDRESS_TYPE_PUBLIC)`. There is
no scan loop: the Bluetooth controller waits for the watch in the background, and after a link loss
a new auto-connect is issued.

- **Android 12+**: the app calls `startObservingDevicePresence`, and the system binds
  `WatchCompanionService` when the watch comes into range, which restarts the link service if the
  process had been killed. The companion association also exempts the app from background
  foreground-service start limits.
- **Android 8–11**: the link service is started from the app, on boot, and when notification access
  connects. Its pending auto-connect does the rest.
- The service restarts after a reboot or app update if a watch is paired.
- **Forget watch** stops the link, removes the Bluetooth bond (if there is one), stops presence
  observation and removes the association. Android has no public API to remove a bond, so the
  hidden `BluetoothDevice.removeBond()` is called by reflection; if that fails the app asks you to
  forget the watch in Android's Bluetooth settings.

### Link layer

- One serialized GATT operation queue: request MTU 185 → discover services → write the CCC
  descriptor → application writes (messages and OTA data). Each operation waits for its callback
  before the next starts. Writes use write-with-response, and their status is checked.
- A write whose callback does not arrive within 5 s means the link is dead: the connection is torn
  down and reconnected with backoff. No further operation is issued on it, so a late callback can
  never be mistaken for the next operation's.
- Watch → phone notifications are reassembled before parsing (`+` chunk, `=` last chunk, anything
  else a whole message; a plain message discards a pending partial; state is cleared on disconnect).
- On top of that, the v2 stop-and-wait layer: every message gets `seq|`, one message is in flight,
  1500 ms ACK timeout, 3 retries with the same seq, `NAK|FULL` waits 500 ms and retries,
  `NAK|PARSE`/`NAK|UNKNOWN` drop the message. A still-queued state message (TIME, PHONE, MUSIC, …)
  is replaced in place if a newer one arrives before it is sent.
- `TIME` is built at the moment of each transmission attempt, so a queued or retried `TIME` is
  never stale.
- State messages (`TIME`, `SETTINGS`, `PHONE`, `NEXT_ALARM`, `EVENT`, `WEATHER`, `MUSIC`) count as
  delivered only when the watch ACKed them. If one is dropped (NAK, retries exhausted, queue
  overflow) the then-current value is sent again after 5 s, doubling up to 5 min while resends keep
  being dropped. After three `PARSE`/`UNKNOWN` NAKs in a
  row for a key the app stops retrying that key until something is ACKed.
- If three messages in a row are dropped for timeout, the watch has stopped answering: the link is
  restarted (with the reconnect backoff, see *Battery*) instead of staying "Connected".
- Text fields are sanitised as the spec requires: NFD, combining marks dropped, `|`→`/`, newline/tab
  →space, other non-ASCII dropped, spaces collapsed, trimmed and cut to the field length. The output
  is pure ASCII, so the limits are byte limits.

### Session

After `HELLO` is ACKed the phone sends `TIME` (UTC + zone offset incl. DST, and the next DST change),
`SETTINGS` (the phone's stored copy), `PHONE`, `NEXT_ALARM`, `EVENT`, `WEATHER` (if known), `MUSIC`,
`NOTIFY_CLEAR|0`, the newest five notifications (oldest first, `alert` 0), a ringing `CALL` if there
is one, and `REQUEST|HISTORY`. If the watch never ACKs `HELLO`, the app drops the link and
reconnects with backoff; after eight failed connections in a row it stops (see *Battery*).

The watch's `HELLO` flags are shown on the status screen: link security (bit 1), Bluetooth update
support (bit 2) and watch storage (bit 4).

### Secure link (bonding)

Nothing changes for a watch with "Secure link" off. With it on, the watch ignores every write until
the link is encrypted and only repeats its own `HELLO` with flag 1. When the app sees that `HELLO`
before a session exists it calls `createBond()` (LE Just Works) if the phone is not bonded, keeps
retrying its own `HELLO` for up to 35 s while pairing runs, and starts the session as soon as the
watch ACKs. If pairing fails, the watch never ACKs, or the link cannot be encrypted although the
phone is bonded (one side lost the other's key), the status reads *Pairing failed — forget the watch
in Android Bluetooth settings (and use Forget phone on the watch), then pair again* and the app
retries with a backoff of at least 30 s. After three failed attempts in a row it stops until
**Reconnect** is tapped, the watch is paired again, or Bluetooth is switched off and on. A GATT
operation refused with insufficient authentication/encryption (status 5, 8, 15 or 137) also starts
bonding, after which discovery, the CCC write and `HELLO` are redone.

### Periodic work

Weather refresh (60 min), calendar re-evaluation (60 min and at the start of the reported event)
and the time resync (24 h) are driven by `AlarmManager.setAndAllowWhileIdle` (inexact, no
exact-alarm permission), so they keep running in Doze. The alarms are cancelled on disconnect and
when the service stops. A partial wake lock covers the work an alarm starts and is released as soon
as the download and the resulting messages are done (30 s at most). See *Battery* for the policies.

## Features

| Feature | How |
|---|---|
| Time | Sent at session start, on time/date/time-zone changes (so about once a day, at midnight), and otherwise when the last one is 24 h old. |
| Notifications | Notification listener. Skips this app, group summaries, ongoing and foreground-service notifications, media notifications, and the progress/transport/service/system/status categories. Identical re-posts are not resent, and content updates of one notification are sent at most every 10 s (the latest content wins). Each notification key gets a stable id (1–65535). `alert` is 1 only the first time a key is forwarded from a live post; content updates, session-start replays and filter changes are silent. Removal sends `NOTIFY_CLEAR`; dismissing on the watch cancels the Android notification. |
| Per-app filter | **Choose apps** lists every app that has posted. Each app has a *forward* switch and a *hide text* option, which sends only the app name and title. |
| One-time codes | When enabled (on by default), a 4–8 digit group (also `123 456` / `123-456`) is replaced with `*` only if a code word (code, OTP, verification, PIN, password, login, …) stands in the same sentence within about 48 characters, in the text or at the adjoining edge of the other field. Never masked: money (currency symbol/code next to it, or decimals), a year 1900–2099 next to a month or inside a date, and digit groups that are part of a phone-number-shaped run (9+ digits, or 7+ after a `+`). Prefixed codes such as `G-123456` are masked even without a code word. |
| Calls | `CATEGORY_CALL` notifications: ringing → `CALL\|RING\|caller`, answered or removed → `CALL\|END`. `CALL_REJECT` uses `TelecomManager.endCall()` when *Reject calls from the watch* is on and the permission is granted (Android 9+). Otherwise it presses the notification's Decline button if there is one. |
| Media | `MediaSessionManager` active-session listener plus `MediaController.Callback`. `MUSIC` is sent only when the state or track changes or the position jumps by more than 3 s (a seek; such corrections are at most 15 s apart), never periodically; `MUSIC\|0\|0\|0\|\|` means nothing is playing. Watch `MEDIA` actions control the active session. |
| Weather | Open-Meteo (no key). Modes: off, approximate phone location, fixed coordinates, or manual values. Refreshes every 60 min while connected, and on connect if the cached forecast is older than 50 min; `WEATHER` is sent only when the values change. WMO codes map to short text. See *Weather by phone location* below. |
| Next alarm | `AlarmManager.getNextAlarmClock()`, resent on `NEXT_ALARM_CLOCK_CHANGED`. |
| Calendar | Optional. The next visible, non-all-day, non-declined event that starts within 24 h, from `CalendarContract.Instances`. Updates when the calendar changes and when that event starts. |
| Phone status | Battery %, charging and Do Not Disturb. Charging and DND changes are sent at once, the battery level in steps of 5 points (and at 100 %). |
| Find phone | `FIND_PHONE\|1` rings the default alarm sound on the alarm stream and vibrates, with a Stop notification. While it rings the alarm volume is raised to at least 70 % and put back afterwards (unless you changed it meanwhile). It stops on `FIND_PHONE\|0`, on Stop, after 2 min, or when the service stops. **Find watch** sends `FIND_WATCH`. |
| Watch status | Shows battery %, voltage, charging state, firmware and flags (from `HELLO`), steps today, a 7-day step chart (`REQUEST\|HISTORY`) and the last sync time. |
| Watch settings | Vibration, quiet mode and the notification alert tone (None/Chirp/Double/Rise/Fall/Triple). A change on the phone sends `SETTINGS`; choosing a tone while connected also sends `TONE_PREVIEW`. A `SETTINGS` from the watch updates the phone's copy. The phone's copy wins at session start; until a tone has been chosen on either side the phone omits the tone field (watch keeps its own). |
| Health Connect | Optional step sync, see below. |
| Firmware update | **Update watch firmware**, see below. |

### Weather by phone location

On Android 10+ a `connectedDevice` foreground service counts as background for location. When the
*phone location* mode is saved the app asks for approximate location and then, after an explanation,
for background location ("Allow all the time"). If that is declined weather keeps working: every fix
the app gets is cached (rounded to about 1 km), the cache is refreshed each time the app is opened,
and the service uses the cached fix. The weather status line says so, shows the age of the fix, and
an **Allow background location** button stays available.

### Health Connect step sync

**Sync steps to Health Connect** (off by default) checks that Health Connect is available
(`HealthConnectClient.getSdkStatus`; it offers the Play Store page when the app is missing or too
old) and asks for the write-steps permission through the Health Connect permission screen. The app
has no `ComponentActivity`, so the library's `PermissionController.createRequestPermissionResultContract()`
is used directly: its `createIntent()` is started with `startActivityForResult` and its
`parseResult()` reads the answer. `androidx.activity` is only present as a transitive dependency.

- `STATUS` (steps today) and `HISTORY` (finished days) are written as one `StepsRecord` per local
  day, from local midnight to min(now, next midnight), with `clientRecordId`
  `epaper-watch-steps-YYYYMMDD`, `clientRecordVersion` = write time in ms (so each write replaces the
  day's record), recording method "automatically recorded" and a device of type watch.
- No record is written for zero steps or when the end would not be after the start. A `STATUS` in
  the first two minutes after midnight is skipped (it carries no date). Unchanged counts are not
  rewritten.
- Writes run on a background dispatcher; any failure only changes the status line under the switch
  ("Last sync …").

### Firmware update over Bluetooth

**Update watch firmware** opens a screen that picks a `.bin` with the system file picker and checks
it before anything is sent: 8…761856 bytes, first word a stack pointer in `0x20000000`–`0x20030000`,
second word a thumb reset vector in `0x08010000`–`0x080C9FFF`; a 16-byte DFU suffix is removed. It
shows the size, the CRC-32 and the watch's current firmware. Sending is enabled only while connected
to a watch whose `HELLO` flags offer OTA (and that exposes the OTA data characteristic).

The transfer runs in the link service, so it survives rotation, leaving the screen and the display
turning off: `OTA_BEGIN` → accept on the watch → chunks of min(240, MTU − 7) bytes, each prefixed
with its 4-byte offset, written with response one at a time through the same GATT queue → `OTA_END`
→ `READY` → the watch installs and restarts (about half a minute; Bluetooth has to be switched on
again at the watch) → `INSTALLED` / `FAILED|code` in the next session, shown with the new firmware
version. During a transfer the service holds a partial wake lock, requests high connection priority
(only while data flows; balanced, then low power again afterwards), keeps the update screen's display on, and sends nothing else except
`OTA_ABORT`; when a transfer ends without a restart the whole session state is sent again. Cancel
sends `OTA_ABORT`. A link drop fails the transfer (the watch discards its copy); start again.
`OTA_BEGIN.version` is the file name without extension (16 characters). USB DFU remains the recovery
path.

## Battery

The app runs all day, and what it sends also decides how long the watch's 130 mAh cell lasts: every
message wakes the watch's CPU and radio, and the connection interval sets its idle radio current.
The decisions are pure functions in `link/PowerPolicy.kt` (tested in `PowerPolicyTest`).

**What is sent when**

| Message | Sent |
|---|---|
| Session start burst | Once per connection: `HELLO`, the seven state messages, `NOTIFY_CLEAR\|0`, up to five notifications, `REQUEST\|HISTORY`. Always complete, because a restarted watch has lost everything. |
| `TIME` | Session start; time, date or zone change (the date change makes this daily); otherwise when the last ACKed one is 24 h old. The watch applies daylight saving by itself from the announced change. |
| `PHONE` | Charging or Do Not Disturb changed; battery level moved 5 points from the value the watch has, or reached 100 %. Other `BATTERY_CHANGED` broadcasts (voltage, temperature) are ignored. |
| `MUSIC` | Track, duration or play state changed; position off by more than 3 s from where the watch has counted to, at most once per 15 s. Callbacks are debounced for 0.5 s. Never periodic. |
| `NOTIFY` | A new notification at once; content updates of the same notification at most every 10 s, latest content wins. Identical re-posts never. |
| `WEATHER`, `EVENT`, `NEXT_ALARM`, `SETTINGS` | Only when the value differs from the one the watch ACKed. |
| `REQUEST` | `HISTORY` once per session; `STATUS`/`HISTORY` otherwise only from the **Refresh status** button. No timer: the watch pushes `STATUS` itself. |
| Resend of a dropped state message | After 5 s, doubling to 5 min until one gets through. |

**Connection priority.** A new connection starts at Android's default (balanced) for the
session-start burst. When the last queued message has been acknowledged the app requests
`CONNECTION_PRIORITY_LOW_POWER` and stays there: messages still flow, each taking a few hundred
milliseconds longer. `CONNECTION_PRIORITY_HIGH` is held only while firmware data is being written and
is given back on every way out of a transfer.

**Timers.** One alarm every 60 min while connected (weather, calendar window, 24 h time resync), one
at the start of the reported calendar event, none while disconnected. The weather download is skipped
while the cached forecast is younger than 50 min, and failures are retried no sooner than 10 min
later. Location is one coarse fix, at most once per download, and only when the last fix is older
than 3 h. Health Connect is not called when the step count has not changed. Nothing in the app
polls; the status screen has no refresh timer.

**Wake locks.** `epaperwatch:work`: taken when one of those alarms fires during a session, released
when the download and the messages it caused are done (30 s cap). `epaperwatch:ota`: held for a
firmware transfer and install (25 min cap).

**Reconnecting.** There is no scanning. While the watch is away the only thing that exists is a
pending `autoConnect` in the Bluetooth controller. After a link loss the next auto-connect is opened
after 1 s; the delay doubles for each reconnect until a session has lasted 60 s (up to 60 s after a
plain loss of range, up to 5 min after errors such as status 133), which damps a link flapping at
the edge of range. Connections that are established but fail (service or notification setup,
`HELLO` not ACKed or rejected, a watch that stops answering or closes the link before a session has
lasted 60 s) are given up after eight in a row, pairing after three failed attempts: the app then
holds no connection and no timer, and the status says so. **Reconnect** in the app, the system
reporting the watch in range (companion presence), switching Bluetooth off and on, or a restart of
the link service (reboot, app update) starts again. Just opening the app does not.

## Permissions

| Permission | When |
|---|---|
| `BLUETOOTH_CONNECT` (12+) / `BLUETOOTH`, `BLUETOOTH_ADMIN` (≤11) | Talking to the watch, bonding. Requested when pairing. |
| `POST_NOTIFICATIONS` (13+) | Connection and find-phone notifications. Requested when pairing. |
| `FOREGROUND_SERVICE`, `FOREGROUND_SERVICE_CONNECTED_DEVICE` | Link service. |
| `REQUEST_COMPANION_RUN_IN_BACKGROUND`, `REQUEST_COMPANION_START_FOREGROUND_SERVICES_FROM_BACKGROUND`, `REQUEST_OBSERVE_COMPANION_DEVICE_PRESENCE` | Companion device background behaviour and presence callbacks. |
| `RECEIVE_BOOT_COMPLETED` | Start on boot. |
| `VIBRATE` | Find phone. |
| `WAKE_LOCK` | Firmware transfer; a few seconds of periodic work in Doze. No runtime prompt. |
| `INTERNET` | Weather. |
| `ACCESS_COARSE_LOCATION` | Only when weather is set to *phone location*. |
| `ACCESS_BACKGROUND_LOCATION` (10+) | Optional, only for weather by phone location while the app is closed. Asked after an explanation; may be declined. |
| `READ_CALENDAR` | Only when the calendar option is turned on. |
| `ANSWER_PHONE_CALLS` | Only when *Reject calls from the watch* is turned on. |
| `android.permission.health.WRITE_STEPS` | Only when *Sync steps to Health Connect* is turned on (granted in Health Connect). |
| Notification access (special) | Notifications, calls and media. |

No scan or fine-location permission is needed: the Companion Device chooser does the scanning.
The manifest also declares the Health Connect permission-rationale activity (for Android 9–13 and,
through an activity alias, for Android 14+) and a `<queries>` entry for the Health Connect package.

## Sleep as Android

The bridge implements connection confirmation, tracking start/stop, pause/suspend, requested batch
size, alarm start/stop/update, notifications, hints, movement upload, snooze, dismiss, pause and
resume, using the v2 `SLEEP_*` messages. Movement is calculated on the watch as the largest
three-axis acceleration change in each interval. The phone converts it from milligravity to m/s²,
batches it, and sends it as `MAX_RAW_DATA`. Mappings to note:

- `SET_PAUSE` (Sleep gives the end time) → `SLEEP_PAUSE|<seconds remaining>`, `0` = resume.
  `SET_SUSPENDED` → one year, or `0` when unsuspended.
- `START_ALARM` / `UPDATE_ALARM` delays are converted from milliseconds to whole seconds
  (rounded up). A negative `DELAY` sends nothing.

Sleep as Android requires third-party wearable packages to be whitelisted on modern Android; the
package is `com.james.epaperwatch`. This hardware has no heart-rate, HRV or SpO2 sensor, so the
companion never fabricates those data.

## Limitations

- **The link is not encrypted unless "Secure link" is enabled on the watch.** Without it,
  notification text is sent in clear; use *hide text* for sensitive apps. With it, pairing is
  "Just Works" (no protection against an active attacker during the first pairing).
- None of the protocol additions in this version (fragmentation, tone, flags, bonding, OTA, Health
  Connect sync) has been tested against a real watch yet.
- Firmware images are not signed; an update needs a button press on the watch instead.
- Short reconnect delays (under 30 s) and the state-retry timer use `Handler` delays, which pause
  in deep sleep; longer reconnect delays are also armed as an alarm. The pending auto-connect itself
  is held by the Bluetooth controller.
- None of the battery policies (connection priority, rate limits, backoff) has been measured on a
  phone or a watch yet.
