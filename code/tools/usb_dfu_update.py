#!/usr/bin/env python3
"""Reboot a running watch into resident ST DFU and install a relocated app."""

from __future__ import annotations

import argparse
import os
import pathlib
import shutil
import struct
import subprocess
import tempfile
import time

from firmware_layout import require_vector_origin


APP_START = 0x08010000
APP_END = 0x080CA000
DFU_VID = "0483"
DFU_PID = "df11"


def find_dfu_util() -> pathlib.Path:
    """dfu-util from PATH, or the copy PlatformIO installs for upload_protocol = dfu.

    STM32CubeProgrammer is deliberately not used: v2.23 does not accept the
    resident loader's memory map and fails with "failed to erase memory".
    """
    command = shutil.which("dfu-util") or shutil.which("dfu-util.exe")
    candidates = [pathlib.Path(command)] if command else []
    core = pathlib.Path(
        os.environ.get("PLATFORMIO_CORE_DIR", pathlib.Path.home() / ".platformio")
    )
    for name in ("dfu-util.exe", "dfu-util"):
        candidates.append(core / "packages/tool-dfuutil/bin" / name)
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise SystemExit(
        "dfu-util was not found. Install it, or run `pio run -e watch` once so "
        "PlatformIO installs its own copy."
    )


def load_application(path: pathlib.Path) -> bytes:
    image = path.read_bytes()
    if len(image) >= 16 and image[-8:-5] == b"UFD" and image[-5] == 16:
        image = image[:-16]
    if len(image) < 8 or len(image) > APP_END - APP_START:
        raise SystemExit("Firmware does not fit the resident application slot.")
    stack, reset = struct.unpack_from("<II", image)
    if not 0x20000000 <= stack <= 0x20030000:
        raise SystemExit(f"Invalid initial stack pointer 0x{stack:08X}.")
    if not APP_START <= (reset & ~1) < APP_END or not reset & 1:
        raise SystemExit(
            f"Firmware is not linked for 0x{APP_START:08X} "
            f"(reset handler 0x{reset:08X}). Build environment 'watch'."
        )
    elf_path = path.with_suffix(".elf")
    if not elf_path.is_file():
        raise SystemExit(
            f"Cannot prove the binary's link address because {elf_path} is missing. "
            "Keep PlatformIO's firmware.elf beside firmware.bin."
        )
    try:
        require_vector_origin(elf_path, APP_START, image[:8])
    except ValueError as exc:
        raise SystemExit(str(exc)) from exc
    return image


def request_dfu(port: str) -> None:
    try:
        import serial
    except ImportError as exc:
        raise SystemExit("pyserial is required: python -m pip install pyserial") from exc

    print(f"Requesting resident DFU on {port}...")
    try:
        with serial.Serial(port, 115200, timeout=1, write_timeout=2) as device:
            device.write(b"f\n")
            device.flush()
    except serial.SerialException as exc:
        raise SystemExit(
            f"Cannot open {port}: {exc}\nClose PlatformIO's serial monitor and retry."
        ) from exc


def run_dfu_util(dfu_util: pathlib.Path, *arguments: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [str(dfu_util), "-d", f"{DFU_VID}:{DFU_PID}", *arguments],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )


def wait_for_dfu(dfu_util: pathlib.Path, timeout_seconds: float) -> None:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        result = run_dfu_util(dfu_util, "-l")
        if "Found DFU" in result.stdout:
            print("Resident STM32 DFU connected.")
            return
        time.sleep(0.25)
    raise SystemExit(
        "Timed out waiting for DFU. The running firmware must be the relocated "
        "'watch' build with the resident bootloader already installed."
    )


def checked(result: subprocess.CompletedProcess, step: str) -> None:
    if result.returncode != 0:
        lines = result.stdout.replace("\r", "\n").splitlines()
        tail = "\n".join(line for line in lines[-6:] if line.strip())
        raise SystemExit(
            f"dfu-util failed to {step} (exit code {result.returncode}):\n{tail}"
        )


def program_application(dfu_util: pathlib.Path, image: bytes) -> None:
    with tempfile.TemporaryDirectory() as directory:
        folder = pathlib.Path(directory)
        source = folder / "application.bin"
        readback = folder / "readback.bin"
        source.write_bytes(image)
        region = f"0x{APP_START:08X}"

        print(f"Erasing and programming {len(image)} bytes...")
        checked(
            run_dfu_util(dfu_util, "-a", "0", "-s", region, "-D", str(source)),
            "program the application",
        )
        # The read-back also carries the "leave" request that starts the
        # application, so the watch only leaves the loader after its flash
        # has been read. A mismatch is reported below; holding button 1 at
        # power-up returns to the loader.
        print("Verifying and starting the application...")
        checked(
            run_dfu_util(
                dfu_util, "-a", "0", "-s", f"{region}:{len(image)}:leave",
                "-U", str(readback),
            ),
            "read the application back",
        )
        if readback.read_bytes() != image:
            raise SystemExit(
                "Verification failed: the flash does not match the firmware. "
                "Hold button 1 while powering up and run this tool again."
            )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("port", help="running watch USB CDC port, for example COM26")
    parser.add_argument("firmware", type=pathlib.Path)
    parser.add_argument("--timeout", type=float, default=15.0)
    args = parser.parse_args()

    dfu_util = find_dfu_util()
    image = load_application(args.firmware)
    request_dfu(args.port)
    wait_for_dfu(dfu_util, args.timeout)
    program_application(dfu_util, image)
    print("USB DFU update verified; the watch is rebooting into the application.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
