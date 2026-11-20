"""Read the watch's power log over USB, save it as CSV and summarise it.

    python tools/power_log.py COM27                 # read, save, summarise
    python tools/power_log.py --csv power_log.csv   # summarise a saved file

The watch writes one record a minute to external flash (console `c` dumps
it, `C` twice erases it). Each record covers the minute before it: time in
Stop 2 (deep sleep), in ordinary Sleep (CPU halted but every clock and the
main regulator on) and awake, what woke it, what kept it out of Stop, the
display refreshes and the Bluetooth traffic. Needs pyserial for reading.
"""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import sys
import time
from collections import Counter
from pathlib import Path

FLAG_NAMES = {
    0: "radio on", 1: "phone connected", 2: "USB power", 3: "front light on",
    4: "fast advertising", 5: "battery read", 6: "watchdog reset", 7: "time set",
}
BLOCK_NAMES = {
    0: "deep sleep switched off", 1: "boot followed a watchdog reset",
    2: "Stop fault", 3: "no watchdog/RTC in Stop", 4: "USB attached",
    5: "first minute after boot", 6: "recent button/console input",
    7: "radio on (mode 1)", 8: "busy", 9: "debugger",
}
BUSY_NAMES = {
    0: "vibration/tone", 1: "buttons", 2: "external flash", 3: "firmware update",
    4: "phone link sending", 5: "console line",
}
WAKE_COLUMNS = ["wake_rtc", "wake_button", "wake_accel", "wake_battery",
                "wake_radio", "wake_usb", "wake_other"]


def read_from_watch(port: str) -> list[str]:
    import serial  # pyserial

    lines: list[str] = []
    with serial.Serial(port, 115200, timeout=0.5) as link:
        time.sleep(0.5)
        link.reset_input_buffer()
        link.write(b"c")
        started = False
        deadline = time.monotonic() + 300
        while time.monotonic() < deadline:
            raw = link.readline()
            if not raw:
                continue
            line = raw.decode(errors="replace").strip()
            if line == "POWERLOG BEGIN":
                started = True
                continue
            if line.startswith("POWERLOG END"):
                return lines
            if started and line:
                lines.append(line)
    raise SystemExit("The watch did not finish the dump (no POWERLOG END).")


def pct(part: float, whole: float) -> str:
    return f"{100.0 * part / whole:5.1f} %" if whole else "    - "


def bits(value: int, names: dict[int, str]) -> list[str]:
    return [name for bit, name in names.items() if value & (1 << bit)]


def summarise(rows: list[dict[str, int]]) -> None:
    boots = [r for r in rows if r["type"] == 1]
    intervals = [r for r in rows if r["type"] == 2]
    print(f"{len(rows)} records: {len(intervals)} one-minute intervals, "
          f"{len(boots)} boots "
          f"({sum(1 for b in boots if b['flags'] & 64)} after a watchdog reset)")
    for b in boots:
        if b["stop_ms"]:
            when = (dt.datetime.utcfromtimestamp(b["local_epoch"]).strftime("%Y-%m-%d %H:%M")
                    if b["local_epoch"] else f"#{b['seq']}")
            print(f"  boot {when}: hang recorded at PC=0x{b['stop_ms']:08X} "
                  f"LR=0x{b['idle_ms']:08X}, watchdog resets in a row: {b['awake_ms']}")
    if not intervals:
        return
    first = next((r for r in rows if r["local_epoch"]), None)
    last = next((r for r in reversed(rows) if r["local_epoch"]), None)
    if first and last:
        fmt = "%Y-%m-%d %H:%M"
        print("From", dt.datetime.utcfromtimestamp(first["local_epoch"]).strftime(fmt),
              "to", dt.datetime.utcfromtimestamp(last["local_epoch"]).strftime(fmt),
              "(watch local time)")

    battery = [r for r in intervals if not r["flags"] & 4]  # not on USB
    on_usb = len(intervals) - len(battery)
    print(f"Intervals on battery: {len(battery)}, on USB power: {on_usb} "
          "(USB time is left out below: it keeps the watch out of deep sleep)")
    sample = battery or intervals

    total_ms = sum(r["stop_ms"] + r["idle_ms"] + r["awake_ms"] for r in sample)
    stop = sum(r["stop_ms"] for r in sample)
    idle = sum(r["idle_ms"] for r in sample)
    awake = sum(r["awake_ms"] for r in sample)
    hours = total_ms / 3.6e6
    print()
    print("Where the time went")
    print(f"  Stop 2 (deep sleep)        {pct(stop, total_ms)}")
    print(f"  Sleep (clocks still on)    {pct(idle, total_ms)}")
    print(f"  Awake                      {pct(awake, total_ms)}")
    minutes = total_ms / 60000 or 1
    print(f"  Wakes                      {sum(r['sleeps'] for r in sample) / minutes:7.1f} a minute")

    # Rough average current, with typical STM32WB55 figures at 64 MHz. Only an
    # order-of-magnitude guide: the radio, display, sensors and LED add to it.
    est_ma = (stop * 0.01 + idle * 2.5 + awake * 7.0) / total_ms if total_ms else 0
    print(f"  Rough MCU current          {est_ma:7.2f} mA "
          f"(assumes Stop 0.01 mA, Sleep 2.5 mA, awake 7 mA)")

    pcts = [(r["local_epoch"] or r["uptime_s"], r["battery_pct"]) for r in sample
            if r["flags"] & 32]
    if len(pcts) >= 2 and hours > 0.2:
        drop = pcts[0][1] - pcts[-1][1]
        print(f"  Battery                    {pcts[0][1]} % -> {pcts[-1][1]} % "
              f"= {drop / hours:5.1f} % an hour on battery")

    print()
    print("What kept it out of deep sleep (share of battery intervals where seen)")
    blocks = Counter()
    for r in sample:
        for name in bits(r["blocks_seen"], BLOCK_NAMES):
            blocks[name] += 1
    for name, count in blocks.most_common():
        print(f"  {name:32} {pct(count, len(sample))}")
    busy = Counter()
    for r in sample:
        for name in bits(r["busy_seen"], BUSY_NAMES):
            busy[name] += 1
    if busy:
        print("  busy because of:", ", ".join(f"{n} ({pct(c, len(sample)).strip()})"
                                             for n, c in busy.most_common()))

    print()
    print("What woke it from Stop (per hour)")
    for column in WAKE_COLUMNS:
        count = sum(r[column] for r in sample)
        if count:
            print(f"  {column[5:]:10} {count / hours:9.1f}")

    print()
    print("State (share of battery intervals)")
    for bit in (0, 1, 4, 3):
        count = sum(1 for r in sample if r["flags"] & (1 << bit))
        print(f"  {FLAG_NAMES[bit]:20} {pct(count, len(sample))}")
    print(f"  display refreshes   {sum(r['renders'] for r in sample) / hours:7.1f} an hour, "
          f"{sum(r['display_cs'] for r in sample) / 100 / hours:6.1f} s an hour refreshing")
    print(f"  Bluetooth           {sum(r['ble_rx'] for r in sample) / hours:7.1f} writes in, "
          f"{sum(r['ble_tx'] for r in sample) / hours:6.1f} notifications out an hour")

    print()
    print("By hour (battery intervals)")
    print("  hour              batt  stop  sleep awake wakes/min refresh  rx/tx")
    buckets: dict[str, list[dict[str, int]]] = {}
    for r in sample:
        if not r["local_epoch"]:
            continue
        key = dt.datetime.utcfromtimestamp(r["local_epoch"]).strftime("%Y-%m-%d %H:00")
        buckets.setdefault(key, []).append(r)
    for key, group in buckets.items():
        t = sum(r["stop_ms"] + r["idle_ms"] + r["awake_ms"] for r in group) or 1
        mins = t / 60000
        print(f"  {key}  {group[-1]['battery_pct']:3d} %"
              f" {100 * sum(r['stop_ms'] for r in group) / t:4.0f}%"
              f" {100 * sum(r['idle_ms'] for r in group) / t:5.0f}%"
              f" {100 * sum(r['awake_ms'] for r in group) / t:5.0f}%"
              f" {sum(r['sleeps'] for r in group) / mins:8.1f}"
              f" {sum(r['renders'] for r in group):7d}"
              f"  {sum(r['ble_rx'] for r in group)}/{sum(r['ble_tx'] for r in group)}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("port", nargs="?", help="watch USB serial port, e.g. COM27")
    parser.add_argument("--csv", type=Path, default=Path("power_log.csv"),
                        help="file to write (with a port) or read (without)")
    args = parser.parse_args()

    if args.port:
        lines = read_from_watch(args.port)
        args.csv.write_text("\n".join(lines) + "\n", encoding="utf-8")
        print(f"Saved {len(lines) - 1} records to {args.csv}")
    elif not args.csv.exists():
        parser.error("give the watch's port, or --csv with a saved log")

    with args.csv.open(newline="", encoding="utf-8") as f:
        rows = [{k: int(v) for k, v in row.items()} for row in csv.DictReader(f)]
    summarise(rows)
    return 0


if __name__ == "__main__":
    sys.exit(main())
