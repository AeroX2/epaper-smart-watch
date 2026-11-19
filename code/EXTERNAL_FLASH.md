# External flash layout (U4, XT25F32F, 4 MB)

Shared contract between the watch application (`code/src`) and the resident bootloader
(`code/bootloader`). The chip is on QUADSPI: CLK PA3, NCS PD3, IO0 PD4, IO1 PD5,
IO2/WP# PD6, IO3/HOLD# PD7.

## Access rules (both programs)

- **Single-line SPI commands only** (0x9F JEDEC ID, 0x05 read status, 0x06 write
  enable, 0x03 read, 0x02 page program ≤ 256 bytes within one page, 0x20 4 KiB sector
  erase, 0xB9 deep power-down, 0xAB release). The chip's QE bit is 0 from the factory,
  so IO2/IO3 are WP#/HOLD#: drive **PD6 and PD7 high as plain GPIO outputs** before any
  command. Do not use quad commands and do not set QE.
- Every wait (busy polling) has a timeout; a missing or dead chip must never hang
  either program. Presence check: JEDEC ID is not 0x000000 / 0xFFFFFF.
- The application puts the chip in deep power-down when idle and releases it (≥ 30 µs)
  before use. The bootloader must send release-from-power-down first, because it can run
  after a reset that left the chip asleep.
- All multi-byte fields are little-endian. CRC-32 is IEEE 802.3 (reflected,
  poly 0xEDB88320, init 0xFFFFFFFF, final XOR 0xFFFFFFFF).

## Map

| Range | Owner | Use |
|---|---|---|
| `0x000000`–`0x00FFFF` | application | Config store (settings, bond keys, step history). Format is private to the application. The bootloader never touches it. |
| `0x100000`–`0x100FFF` | both | OTA header sector |
| `0x101000`–`0x1BAFFF` | application writes, bootloader reads | Staged firmware image (max 761856 bytes) |
| `0x200000`–`0x3FFFFF` | application | Power log: a ring of 64-byte `PowerLogRecord`s (`include/watch_power_log.h`), one a minute, about 22 days. Records carry a sequence number and a CRC-32; the oldest 4 KiB sector is erased when the ring wraps. Console `c` dumps it, `C` twice erases it; `tools/power_log.py` reads and summarises it. The bootloader never touches it. |
| everything else | — | unused |

## OTA header (at `0x100000`)

| Offset | Field | Written by | Value |
|---|---|---|---|
| 0 | `magic` | app, at OTA_BEGIN | `0x41544F57` ("WOTA") |
| 4 | `format` | app | `1` |
| 8 | `image_size` | app | bytes |
| 12 | `image_crc32` | app | CRC-32 of the image |
| 16 | `header_crc32` | app | CRC-32 of bytes 0–15 |
| 20 | `ready` | app, only after the staged copy verified | `0x59444552` ("REDY"); erased = `0xFFFFFFFF` |
| 24 | `result` | bootloader | erased `0xFFFFFFFF` = not attempted; `1` = installed; other values = failure code below |

Flash can only clear bits, so `ready` and `result` are each programmed once into the
erased sector; the application erases the header sector to start a new transfer or after
it has reported `result`.

## Bootloader behaviour

Order at reset: (1) DFU request / recovery button → USB DFU as today; (2) otherwise
check for a staged update; (3) otherwise start the application if valid, else USB DFU.

A staged update is installed when `magic`, `format` and `header_crc32` are right,
`ready` == "REDY", `result` == `0xFFFFFFFF`, and `image_size` is 8…761856. Then:

1. Read the image back from external flash and check `image_crc32`. Check its vector
   table the same way `applicationIsValid()` does (stack in SRAM, reset vector inside
   `0x08010000`–`0x080C9FFF`, thumb bit set). On failure write `result` 2 (CRC) or
   3 (bad image) and continue to the normal boot — the internal application is untouched.
2. Erase the internal pages the image covers, program it, and verify the internal copy
   against `image_crc32`. On a flash error or mismatch, retry the whole step once; if it
   still fails write `result` 4 (program/verify) and stay in USB DFU.
3. On success write `result` 1 and start the application.

If power is lost during step 2, `result` is still erased, so the next boot repeats the
install. Result codes: `1` installed, `2` staged CRC mismatch, `3` not a valid image,
`4` internal program/verify failed.

The application reports a non-erased `result` to the phone (`OTA_STATE|INSTALLED|1` or
`OTA_STATE|FAILED|<code>`) in the next session and then erases the header sector. If the
application boots and finds `ready` set but `result` still erased, the installed
bootloader predates OTA support: it reports `OTA_STATE|FAILED|0` and erases the header.
