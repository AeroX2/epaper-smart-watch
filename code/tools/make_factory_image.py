#!/usr/bin/env python3
"""Combine the resident ST DFU loader and relocated app for first install."""

from __future__ import annotations

import argparse
import pathlib
import struct

from firmware_layout import require_vector_origin


APP_OFFSET = 0x10000
APP_LIMIT = 0xCA000


def load_raw_binary(path: pathlib.Path) -> bytes:
    image = path.read_bytes()
    if len(image) >= 16 and image[-8:-5] == b"UFD" and image[-5] == 16:
        image = image[:-16]
    return image


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("bootloader", type=pathlib.Path)
    parser.add_argument("application", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    args = parser.parse_args()

    bootloader = load_raw_binary(args.bootloader)
    application = load_raw_binary(args.application)
    if len(bootloader) > APP_OFFSET:
        raise SystemExit(
            f"bootloader is {len(bootloader)} bytes; reserved slot is {APP_OFFSET}"
        )
    if len(application) > APP_LIMIT - APP_OFFSET:
        raise SystemExit("application does not fit in the OTA application slot")
    if len(application) < 8:
        raise SystemExit("application is too small to contain a vector table")
    application_elf = args.application.with_suffix(".elf")
    if not application_elf.is_file():
        raise SystemExit(
            f"cannot prove application link address: {application_elf} is missing"
        )
    try:
        require_vector_origin(application_elf, 0x08010000, application[:8])
    except ValueError as exc:
        raise SystemExit(str(exc)) from exc
    initial_stack, reset_handler = struct.unpack_from("<II", application)
    if not 0x20000000 <= initial_stack <= 0x20030000:
        raise SystemExit(
            f"application has invalid initial stack pointer 0x{initial_stack:08X}"
        )
    if not 0x08010001 <= reset_handler < 0x080CA000 or not reset_handler & 1:
        raise SystemExit(
            f"application is not linked for 0x08010000 "
            f"(reset handler 0x{reset_handler:08X})"
        )
    combined = bootloader + b"\xff" * (APP_OFFSET - len(bootloader)) + application
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(combined)
    print(
        f"Wrote {args.output} ({len(combined)} bytes): bootloader at 0x08000000, "
        "application at 0x08010000"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
