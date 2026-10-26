#!/usr/bin/env python3
"""Fail closed on a non-Windows/wrong-machine PE, or absent real .rsrc."""
import pathlib
import struct
import sys

if len(sys.argv) not in (3, 4) or sys.argv[2] not in ("x86_64", "aarch64") or (
    len(sys.argv) == 4 and sys.argv[3] != "--require-resources"
):
    raise SystemExit("verify_pe_machine.py FILE x86_64|aarch64 [--require-resources]")
data = pathlib.Path(sys.argv[1]).read_bytes()
if len(data) < 128 or data[:2] != b"MZ":
    raise SystemExit("Expected Windows MZ executable")
offset = struct.unpack_from("<I", data, 0x3C)[0]
if offset < 64 or offset + 24 > len(data) or data[offset:offset+4] != b"PE\0\0":
    raise SystemExit("Missing PE/COFF header")
machine, sections = struct.unpack_from("<HH", data, offset + 4)
expected = {"x86_64": 0x8664, "aarch64": 0xAA64}[sys.argv[2]]
if machine != expected:
    raise SystemExit(f"Wrong PE machine: {machine:#x} != {expected:#x}")
if len(sys.argv) == 4:
    opt_size = struct.unpack_from("<H", data, offset + 20)[0]
    opt = offset + 24
    section_table = opt + opt_size
    if opt_size < 136 or section_table + sections * 40 > len(data):
        raise SystemExit("Invalid PE32+ optional header or section table")
    if struct.unpack_from("<H", data, opt)[0] != 0x20B:
        raise SystemExit("Expected PE32+ image")
    rva, size = struct.unpack_from("<II", data, opt + 112 + 8 * 2)
    names = [
        data[section_table + i * 40:section_table + i * 40 + 8].rstrip(b"\0")
        for i in range(sections)
    ]
    if rva == 0 or size == 0 or b".rsrc" not in names:
        raise SystemExit("Missing nonempty Windows resource directory")
print(f"Validated PE {sys.argv[2]} {sys.argv[1]}" +
      (" with .rsrc" if len(sys.argv) == 4 else ""))
