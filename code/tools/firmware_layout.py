"""Small dependency-free ELF32 layout checks used by firmware packaging tools."""

from __future__ import annotations

import pathlib
import struct


def section_address(path: pathlib.Path, wanted_name: str) -> tuple[int, bytes]:
    image = path.read_bytes()
    if image[:4] != b"\x7fELF" or image[4] != 1 or image[5] != 1:
        raise ValueError(f"{path} is not a 32-bit little-endian ELF file")
    section_offset = struct.unpack_from("<I", image, 0x20)[0]
    section_entry_size, section_count, names_index = struct.unpack_from(
        "<HHH", image, 0x2E
    )
    if section_entry_size < 40 or names_index >= section_count:
        raise ValueError(f"{path} has an invalid section table")

    def section(index: int) -> tuple[int, int, int, int]:
        offset = section_offset + index * section_entry_size
        if offset + 40 > len(image):
            raise ValueError(f"{path} has a truncated section table")
        name_offset = struct.unpack_from("<I", image, offset)[0]
        address, data_offset, data_size = struct.unpack_from("<III", image, offset + 12)
        return name_offset, address, data_offset, data_size

    _, _, names_offset, names_size = section(names_index)
    names = image[names_offset : names_offset + names_size]
    for index in range(section_count):
        name_offset, address, data_offset, data_size = section(index)
        if name_offset >= len(names):
            continue
        name_end = names.find(b"\0", name_offset)
        if name_end < 0:
            continue
        name = names[name_offset:name_end].decode("ascii", errors="replace")
        if name == wanted_name:
            data = image[data_offset : data_offset + data_size]
            if len(data) != data_size:
                raise ValueError(f"{path} has a truncated {wanted_name} section")
            return address, data
    raise ValueError(f"{path} has no {wanted_name} section")


def require_vector_origin(
    elf_path: pathlib.Path, expected_address: int, expected_vector: bytes | None = None
) -> None:
    address, vector = section_address(elf_path, ".isr_vector")
    if address != expected_address:
        raise ValueError(
            f"{elf_path} vector table is at 0x{address:08X}, expected "
            f"0x{expected_address:08X}"
        )
    if expected_vector is not None and vector[: len(expected_vector)] != expected_vector:
        raise ValueError(f"{elf_path} does not match the supplied binary")
