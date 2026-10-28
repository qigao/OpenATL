#!/usr/bin/env python3
"""Fail closed on a non-Windows/wrong-machine PE, or absent real .rsrc."""
import pathlib
import struct
import sys

if len(sys.argv) not in (3, 4) or sys.argv[2] not in ("i686", "x86_64", "aarch64") or (
    len(sys.argv) == 4 and sys.argv[3] != "--require-resources"
):
    raise SystemExit("verify_pe_machine.py FILE i686|x86_64|aarch64 [--require-resources]")
data = pathlib.Path(sys.argv[1]).read_bytes()
if len(data) < 128 or data[:2] != b"MZ":
    raise SystemExit("Expected Windows MZ executable")
offset = struct.unpack_from("<I", data, 0x3C)[0]
if offset < 64 or offset + 24 > len(data) or data[offset:offset+4] != b"PE\0\0":
    raise SystemExit("Missing PE/COFF header")
machine, sections = struct.unpack_from("<HH", data, offset + 4)
expected = {"i686": 0x014C, "x86_64": 0x8664, "aarch64": 0xAA64}[sys.argv[2]]
if machine != expected:
    raise SystemExit(f"Wrong PE machine: {machine:#x} != {expected:#x}")
if len(sys.argv) == 4:
    opt_size = struct.unpack_from("<H", data, offset + 20)[0]
    opt = offset + 24
    section_table = opt + opt_size
    is_pe32 = sys.argv[2] == "i686"
    directory_offset = 96 if is_pe32 else 112
    minimum_size = directory_offset + 8 * 3
    if opt_size < minimum_size or section_table + sections * 40 > len(data):
        raise SystemExit("Invalid PE optional header or section table")
    if struct.unpack_from("<H", data, opt)[0] != (0x10B if is_pe32 else 0x20B):
        raise SystemExit("Unexpected PE32/PE32+ optional header")
    rva, size = struct.unpack_from("<II", data, opt + directory_offset + 8 * 2)
    names = [
        data[section_table + i * 40:section_table + i * 40 + 8].rstrip(b"\0")
        for i in range(sections)
    ]
    if rva == 0 or size == 0 or b".rsrc" not in names:
        raise SystemExit("Missing nonempty Windows resource directory")
print(f"Validated PE {sys.argv[2]} {sys.argv[1]}" +
      (" with .rsrc" if len(sys.argv) == 4 else ""))
