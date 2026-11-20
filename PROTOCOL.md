# Watch ↔ phone BLE protocol (v2)

This is the contract between the watch firmware (`code/`) and the Android companion
(`companion/`). Both sides must implement exactly this. Version 1 (the comma-based
`TIME,…` / `NOTIFY,…` messages) is retired; there is no backwards compatibility.

## GATT layout

| Item | UUID | Properties | Size |
|---|---|---|---|
| Watch service | `7BD10000-6B10-4A21-9D6A-3A56B35C1000` | advertised | |
| Phone → watch | `7BD10001-6B10-4A21-9D6A-3A56B35C1000` | Write, Write Without Response | 160 bytes |
| Watch → phone | `7BD10002-6B10-4A21-9D6A-3A56B35C1000` | Read, Notify | 160 bytes |
| OTA data | `7BD10003-6B10-4A21-9D6A-3A56B35C1000` | Write (with response) | 244 bytes |
| Battery service | `180F` / level `2A19` | Read, Notify | 1 byte |

The phone requests MTU 185 and only starts writing after the MTU exchange and the CCC
(notification) descriptor write have both completed.

## Framing

- One message per GATT write (phone → watch) or notification (watch → phone).
- A message is ASCII, at most **160 bytes**, fields separated by `|`. No terminator.
- **Text fields** contain only printable ASCII `0x20`–`0x7E` and never `|`. The phone
  sanitises before sending: Unicode is NFD-normalised, combining marks are dropped,
  `|` becomes `/`, newlines/tabs become spaces, anything else non-ASCII is dropped,
  runs of spaces collapse, and the field is trimmed and cut to its maximum length.
  The watch must still tolerate (and replace with `?`) any byte outside that range.
- Numbers are decimal ASCII. Missing optional numeric fields read as 0.
- All absolute times on the wire are **UTC Unix seconds**. The watch converts to local
  time with the offset from the last `TIME` message.

## Reliability (phone → watch)

Every phone → watch message starts with a sequence number:

```
<seq>|<TYPE>|<field>|<field>…
```

- `seq` is 1–65535 and increments per new message (wrapping 65535 → 1).
- The watch replies `ACK|<seq>` once the message is parsed and queued for the
  application, or `NAK|<seq>|<reason>` (`PARSE`, `UNKNOWN`, `FULL`) if it was not.
- **Stop-and-wait:** the phone sends the next message only after the ACK/NAK for the
  previous one. If neither arrives within **1500 ms**, it resends the same message with
  the same seq, up to **3** retries, then drops it and moves on.
- The watch remembers the last accepted seq. A repeat of that seq is re-ACKed and
  **not** applied twice. `HELLO` resets this memory (any seq is accepted after it).
- On `NAK|…|FULL` the phone waits 500 ms and resends (counts as a retry).
- The watch keeps a queue of at least **8** parsed messages, so bursts are not lost
  while the main loop is busy (e.g. during an e-paper refresh).

Watch → phone messages carry **no** sequence number and are not acknowledged. The
watch queues them (at least 8) and sends them in order while a phone is subscribed;
the queue is cleared on disconnect.

### Fragmentation (watch → phone)

A notification is silently truncated to the ATT MTU minus 3, and the watch cannot rely
on a large MTU. So the watch never sends a notification longer than **20 bytes**:

- A message of at most 20 bytes is sent as is. It always starts with `A`–`Z`.
- A longer message is split into chunks of at most 19 payload bytes. Every chunk but
  the last is sent as `+` followed by its payload; the last as `=` followed by its
  payload. Chunks of one message are sent consecutively, never interleaved with
  another message.
- The phone concatenates `+` payloads and completes the message on `=`. A plain
  message arriving while a fragment is pending discards the pending partial.
  Reassembly state is cleared on disconnect.

Phone → watch writes are not fragmented by the protocol: the ATT layer's prepared
(long) writes carry them when the MTU is small.

## Session start

1. Phone connects, negotiates MTU, discovers services, enables notifications.
2. Phone sends `HELLO` (seq'd). Watch replies `ACK`, then `HELLO` and `STATUS`.
3. Phone then sends, in order: `TIME`, `SETTINGS`, `PHONE`, `NEXT_ALARM`, `EVENT`,
   `BUTTONS`, `MUSIC`, and current notifications (newest last, max 5).

## Phone → watch messages

| Type | Fields | Meaning |
|---|---|---|
| `HELLO` | `proto`, `app_version` | `proto` = `2`. Resets the watch's seq memory. |
| `TIME` | `utc`, `offset`, `next_change_utc`, `offset_after` | `offset` = seconds east of UTC including DST (e.g. 39600). Watch sets its RTC to local time = `utc + offset` and stores `offset`. `next_change_utc` / `offset_after` (optional, `0` = none) announce the zone's next offset change (daylight saving); the watch applies it by itself when that moment passes. Sent at session start and whenever the phone's time, date or zone changes. |
| `NOTIFY` | `id`, `app`(24), `title`(28), `text`(56), `alert` | Add or replace notification `id` (1–65535). Watch keeps the newest 5. `alert` `1` = a new arrival: vibrate / play the alert tone (subject to the watch's vibration, quiet and tone settings); `0` or missing = silent (session-start replays, and content updates that would not alert on the phone either). A re-post the phone itself alerts for, such as a new message in an existing chat, is sent with `1`. |
| `NOTIFY_CLEAR` | `id` | Remove notification `id`; `0` clears all. |
| `CALL` | `state`, `caller`(28) | `state` = `RING` or `END`. On `RING` the watch shows the call screen and alerts (unless quiet). `END` closes it. |
| `MUSIC` | `state`, `position`, `duration`, `title`(24), `artist`(24) | `state`: `0` none, `1` playing, `2` paused. `position`/`duration` in seconds at the moment of sending; while playing the watch advances position itself. `0|0|0||` = nothing playing. Sent on change only, not periodically. |
| `NEXT_ALARM` | `utc` | Phone's next alarm clock, `0` = none. |
| `EVENT` | `utc`, `title`(28) | Next calendar event starting within 24 h, `0|` = none. |
| `PHONE` | `battery`, `charging`, `dnd` | Phone battery %, `1`/`0` charging, `1`/`0` Do Not Disturb active. |
| `SETTINGS` | `vibration`, `quiet`, `tone` | `vibration`/`quiet` `1`/`0`. `tone` = notification alert tone: `0` none, `1` Chirp, `2` Double, `3` Rise, `4` Fall, `5` Triple (missing = unchanged). Phone's stored copy wins at session start. Quiet mode silences the tone; vibration is governed by `vibration`. |
| `BUTTONS` | `tl`, `tr`, `bl`, `br` | Button layout: the role of the top-left, top-right, bottom-left and bottom-right button, `0` Home/back, `1` Next, `2` Previous, `3` Select. Each role exactly once, else `NAK\|seq\|PARSE`. Stored in the watch's flash; the phone sends it at session start after `SETTINGS`. Default `3\|1\|0\|2` (top-left Select, top-right Next, bottom-left Home, bottom-right Previous). |
| `TONE_PREVIEW` | `tone` | Play tone `1`–`5` once now (ignores quiet mode). |
| `FIND_WATCH` | — | Watch alerts (vibrate + buzzer) for ~10 s or until any button. |
| `CAMERA` | `state\|number\|detail` | The phone's camera screen for the watch's Camera app. `state`: `CLOSED`, `OPENING`, `READY`, `COUNTDOWN` (`number` = seconds), `SAVED` (`number` = photos since it opened) or `ERROR` (`detail`, ≤ 20 chars, e.g. `NO PERMISSION`, `CAMERA BUSY`, `TAP THE PHONE`, `PHOTO FAILED`). |
| `REQUEST` | `what` | `STATUS` or `HISTORY`. |
| `STEPS_RESET` | — | Reset today's step count. |

## Watch → phone messages

| Type | Fields | Meaning |
|---|---|---|
| `ACK` / `NAK` | `seq` (, `reason`) | See reliability. |
| `HELLO` | `proto`, `fw_version`, `flags` | Sent after receiving `HELLO`. `flags` is a decimal bit field: `1` = link encryption required (bonded), `2` = Bluetooth firmware update supported (resident-bootloader build with working external flash), `4` = external flash storage working. |
| `STATUS` | `battery`, `millivolts`, `steps`, `charging` | Sent after `HELLO`, on `REQUEST|STATUS`, when battery % changes, and at most every 10 min otherwise. `charging` `1`/`0`/`-1` unknown. |
| `HISTORY` | `yyyymmdd`, `steps` | One message per stored day (up to 7, oldest first), on `REQUEST|HISTORY`. Followed by `HISTORY_END`. |
| `HISTORY_END` | — | |
| `NOTIFY_DISMISS` | `id` | User dismissed it on the watch; phone cancels the Android notification. |
| `MEDIA` | `action` | `PREV`, `NEXT`, `PLAY_PAUSE`. |
| `CALL_REJECT` | — | Reject the ringing call. |
| `FIND_PHONE` | `on` | `1` start ringing the phone, `0` stop. |
| `CAMERA` | `action\|delay` | `OPEN` the phone's camera screen, `SHUTTER` take a photo after `delay` seconds (0, 3 or 10), `CLOSE` it. The phone answers with `CAMERA` state messages. |
| `SETTINGS` | `vibration`, `quiet`, `tone` | Sent when changed on the watch. |

## Firmware update over Bluetooth (OTA)

Only offered when the watch's `HELLO` flags include `2`. The image is the same
`firmware.bin` that USB DFU installs (linked for `0x08010000`, at most 761856 bytes).
It is staged in the external flash and installed by the resident bootloader after a
reboot; USB DFU remains the recovery path. Layout and bootloader behaviour are in
`code/EXTERNAL_FLASH.md`.

Phone → watch (ordinary seq'd messages):

| Type | Fields | Meaning |
|---|---|---|
| `OTA_BEGIN` | `size`, `crc32`, `version`(16) | Image size in bytes and its CRC-32 (IEEE 802.3, unsigned decimal). The watch asks the wearer to confirm on the watch (select = accept, back = decline; 60 s timeout). ACK only means "received". |
| `OTA_END` | — | All bytes written. Watch verifies size and CRC over the staged copy. |
| `OTA_ABORT` | — | Cancel; watch discards the staged data. |

Watch → phone:

| Type | Fields | Meaning |
|---|---|---|
| `OTA_STATE` | `state`, `detail` | `ACCEPT` (`detail` = max data bytes per chunk the watch will take, at most 240): start sending. `DECLINED`. `BUSY`. `UNSUPPORTED`. `PROGRESS` (`detail` = next expected offset; sent about every 16 KiB and after any rejected chunk). `ERROR` (`detail` = `SIZE`, `CRC`, `IMAGE`, `FLASH`, `TIMEOUT`). `READY` (verified; the watch reboots about 2 s later to install). `INSTALLED` / `FAILED` (`detail` = bootloader result code): reported once in the first session after the reboot. |

Data: each write to the **OTA data** characteristic is `offset` (4 bytes, little-endian)
followed by 1–N payload bytes (N from `ACCEPT`, and never more than MTU − 3 − 4: the
phone must not rely on long writes here). Writes use Write **with** response, one at a
time, in order. The watch accepts a chunk only if `offset` equals the number of bytes
received so far; otherwise it ignores the chunk and sends `OTA_STATE|PROGRESS|<expected>`
so the phone resumes from there. The transfer times out after 30 s without an accepted
chunk (`OTA_STATE|ERROR|TIMEOUT`).

During a transfer the phone sends no other messages except `OTA_ABORT`, and the watch
suspends `STATUS`/`HISTORY`.

## Link encryption (optional)

Off by default. "Secure link" is a watch setting (Settings, persisted in external
flash; needs working storage). When it is on, the watch's `HELLO` flags include `1`
and the link must be encrypted with a bond before the watch will talk:

- The GATT characteristics themselves stay open (the BLE library's encrypted-attribute
  path is not safe to use), so the phone gets **no** ATT "insufficient encryption"
  error. Enforcement is in the watch application: on an unencrypted link it parses,
  applies and acknowledges nothing, refuses OTA data, and sends nothing except an
  unsolicited `HELLO|2|<fw>|<flags>` at most once per second.
- The phone therefore starts bonding itself: on seeing flag `1` while not bonded it
  calls `createBond()` (LE "Just Works", no passkey), waits for the bond, and keeps
  retrying its own `HELLO`, which the watch ACKs once the link is encrypted.
- The watch stores one bond and accepts a new pairing only while it has none. Turning
  the setting off, or "Forget phone" on the watch, erases the key and restarts the
  radio; the phone must remove its bond too or the link will fail to encrypt.

## Clarifications (as implemented)

- The watch advertises with its fixed **public** address (derived from the chip's unique
  device number), so the phone can reconnect by stored address across watch restarts.

- Empty trailing text fields are still sent, e.g. `CALL|END|`, `EVENT|0|`, `MUSIC|0|0|0||`.
- If the watch cannot read the seq at all it replies `NAK|0|PARSE`. Out-of-range numbers
  (e.g. battery > 100, temperature outside ±99) are `NAK|<seq>|PARSE`.
- `STATUS` reports `battery` `-1` and `millivolts` `0` when the fuel gauge is unreadable;
  `charging` is always `-1` on current hardware (no charger-status line reaches the MCU).
- `HISTORY` holds 6 finished days plus today (today last), RAM only — lost on reset.
- `HELLO.app_version` is at most 20 characters.
- Session start, as implemented by the phone: after the list above it sends
  `NOTIFY_CLEAR|0` before the notifications, then a ringing `CALL` if any, then
  `REQUEST|HISTORY`.
- The watch's outbound queue lets an `ACK`/`NAK` displace the oldest queued message
  rather than be dropped.
- OTA: `ACCEPT` always carries `240`, followed by `|1` from firmware that receives
  full-size packets; the phone clamps each chunk to min(240, MTU − 7), and to 96 when
  the `1` is missing (earlier firmware dropped any packet over 128 bytes). The on-watch confirmation timing out is reported as `DECLINED`.
  `OTA_BEGIN` while the watch is ringing (alarm/call) yields `BUSY`. `OTA_END` with no
  transfer in progress yields `ERROR|SIZE`. `OTA_ABORT` has no reply beyond its ACK.
  `PROGRESS` is also sent once when the last byte has arrived. After `READY` the watch
  draws an "installing" screen and resets; the install takes about 15–30 s, and the
  watch switches its radio back on by itself if it was on before the update.
- Notification alert tones are suppressed while an alarm, call or find-my-watch alert
  is sounding.

## Not in v2 (deliberately)

- Passkey / numeric-comparison pairing (the link is encrypted but not protected against
  an active man-in-the-middle during the first pairing).
- Signed firmware images. An OTA needs a button press on the watch instead.
