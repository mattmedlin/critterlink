#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Deterministically pack sum.S's literal words into a sectionless ELF32 file.

This is a fixture-specific packer, not a general assembler or PS2 SDK. No guest
code is executed here. Fixed addresses/BSS allocation are the fixture's link map.
"""
import argparse
from pathlib import Path
import struct


def build(source: Path) -> bytes:
    sections = {".text": bytearray(), ".data": bytearray()}
    current = None
    for number, line in enumerate(source.read_text(encoding="utf-8").splitlines(), 1):
        line = line.partition("#")[0].strip()
        if not line or line in (".globl _start", "_start:"):
            continue
        if line.startswith(".section "):
            current = line.split()[1]
            if current not in sections:
                raise ValueError(f"line {number}: unsupported section {current}")
        elif line.startswith(".word ") and current is not None:
            value = int(line[6:].strip(), 0)
            if not 0 <= value <= 0xFFFFFFFF:
                raise ValueError(f"line {number}: word outside 32-bit range")
            sections[current].extend(struct.pack("<I", value))
        else:
            raise ValueError(f"line {number}: expected a section or literal .word")
    text, data = sections[".text"], sections[".data"]
    if len(text) != 80 or len(data) != 4:
        raise ValueError("fixture link map requires 20 text words and one data word")
    ident = b"\x7fELF\x01\x01\x01" + bytes(9)
    header = struct.pack("<16sHHIIIIIHHHHHH", ident, 2, 8, 1, 0x00100000,
                         52, 0, 0x20000001, 52, 32, 2, 0, 0, 0)
    code_segment = struct.pack("<8I", 1, 0x100, 0x00100000, 0x00100000,
                               len(text), len(text), 5, 0x100)
    data_segment = struct.pack("<8I", 1, 0x200, 0x00101000, 0x00101000,
                               len(data), 0x24, 6, 0x100)
    image = bytearray(0x204)
    image[:116] = header + code_segment + data_segment
    image[0x100:0x100 + len(text)] = text
    image[0x200:] = data
    return bytes(image)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path(__file__).with_name("sum.S"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    image = build(args.source)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(image)
