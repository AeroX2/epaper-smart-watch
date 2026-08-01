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
DFU_VID = "0x0483"
DFU_PID = "0xDF11"


def find_cube_programmer() -> pathlib.Path:
    candidates = [
        pathlib.Path(os.environ.get("ProgramFiles", r"C:\Program Files"))
        / "STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe",
        pathlib.Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"))
        / "STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe",
    ]
    command = shutil.which("STM32_Programmer_CLI") or shutil.which(
        "STM32_Programmer_CLI.exe"
    )
    if command:
        candidates.insert(0, pathlib.Path(command))
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise SystemExit(
        "STM32CubeProgrammer CLI was not found. Install STM32CubeProgrammer "
        "or add STM32_Programmer_CLI to PATH."
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


def wait_for_dfu(programmer: pathlib.Path, timeout_seconds: float) -> None:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        result = subprocess.run(
            [
                str(programmer),
                "-c",
                "port=USB1",
                f"VID={DFU_VID}",
                f"PID={DFU_PID}",
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            check=False,
        )
        if result.returncode == 0:
            print("Resident STM32 DFU connected.")
            return
        time.sleep(0.25)
    raise SystemExit(
        "Timed out waiting for DFU. The running firmware must be the relocated "
        "'watch' build with the resident bootloader already installed."
    )


def program_application(programmer: pathlib.Path, image: bytes) -> None:
    temporary_path: pathlib.Path | None = None
    try:
        with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as temporary:
            temporary.write(image)
            temporary_path = pathlib.Path(temporary.name)
        command = [
            str(programmer),
            "-c",
            "port=USB1",
            f"VID={DFU_VID}",
            f"PID={DFU_PID}",
            "-d",
            str(temporary_path),
            f"0x{APP_START:08X}",
            "-v",
            "-s",
            f"0x{APP_START:08X}",
        ]
        print(f"Programming and verifying {len(image)} bytes...")
        result = subprocess.run(command, check=False)
        if result.returncode != 0:
            raise SystemExit(f"STM32CubeProgrammer failed with exit code {result.returncode}.")
    finally:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("port", help="running watch USB CDC port, for example COM26")
    parser.add_argument("firmware", type=pathlib.Path)
    parser.add_argument("--timeout", type=float, default=15.0)
    args = parser.parse_args()

    programmer = find_cube_programmer()
    image = load_application(args.firmware)
    request_dfu(args.port)
    wait_for_dfu(programmer, args.timeout)
    program_application(programmer, image)
    print("USB DFU update verified; the watch is rebooting into the application.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
