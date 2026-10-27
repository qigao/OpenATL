#!/usr/bin/env python3
"""Strict PE32+ CFG/CET, DEP and ASLR evidence, independent of build flags."""
from pathlib import Path
import struct
import sys


def main(path, machine, cfg, cet):
    data = Path(path).read_bytes()

    def read(fmt, at):
        if at < 0 or at + struct.calcsize(fmt) > len(data):
            raise ValueError(f"out-of-bounds PE data at {at:#x}")
        return struct.unpack_from(fmt, data, at)

    if data[:2] != b"MZ":
        raise ValueError("MZ missing")
    nt = read("<I", 0x3c)[0]
    if data[nt:nt + 4] != b"PE\0\0":
        raise ValueError("PE signature missing")
    number = read("<H", nt + 6)[0]
    opt_len = read("<H", nt + 20)[0]
    opt = nt + 24
    if (read("<H", nt + 4)[0] != {"x86_64": 0x8664, "aarch64": 0xaa64}[machine]:
        raise ValueError("wrong PE machine")
    if opt_len < 240 or read("<H", opt)[0] != 0x20b:
        raise ValueError("expected PE32+ and 16 directories")
    dll = read("<H", opt + 70)[0]
    if (dll & 0x140) != 0x140:
        raise ValueError(f"NX_COMPAT or DYNAMIC_BASE missing ({dll:#x})")
    sections = [read("<IIII", opt + opt_len + i * 40 + 8) for i in range(number)]

    def offset(rva, length):
        for _, va, raw_length, raw_offset in sections:
            if va <= rva and rva + length <= va + raw_length:
                pos = raw_offset + rva - va
                if pos + length <= len(data):
                    return pos
        raise ValueError(f"invalid RVA {rva:#x}, bytes={length}")

    def directory(i):
        return read("<II", opt + 112 + i * 8)

    guard = bool(dll & 0x4000)
    flags = 0
    load_rva, load_size = directory(10)
    if load_rva and load_size >= 0x94:
        flags = read("<I", offset(load_rva, 0x94) + 0x90)[0]
    if cfg and (not guard or flags & 0x500 != 0x500):
        raise ValueError(f"CFG not fully instrumented: dll={dll:#x}, flags={flags:#x}")

    cet_found = False
    debug_rva, debug_len = directory(6)
    if debug_rva and debug_len:
        if debug_len % 28:
            raise ValueError("truncated debug directory")
        start = offset(debug_rva, debug_len)
        for pos in range(start, start + debug_len, 28):
            typ = read("<I", pos + 12)[0]
            size = read("<I", pos + 16)[0]
            file_pos = read("<I", pos + 24)[0]
            if typ == 20:
                if size < 4:
                    raise ValueError("bad extended DLL characteristics record")
                cet_found = cet_found or bool(read("<I", file_pos)[0] & 1)
    if cet and not cet_found:
        raise ValueError("CETCOMPAT debug directory attribute absent")
    print(f"OPENATL_PE_MITIGATIONS_OK arch={machine} nx=1 aslr=1 "
          f"cfg={int(guard and flags & 0x500 == 0x500)} cet={int(cet_found)}")


if __name__ == "__main__":
    if len(sys.argv) < 3 or sys.argv[2] not in {"x86_64", "aarch64"}:
        raise SystemExit("verify_pe_mitigations.py FILE ARCH [--require-cfg] [--require-cet]")
    args = set(sys.argv[3:])
    if not args <= {"--require-cfg", "--require-cet"}:
        raise SystemExit(f"unsupported flags: {args}")
    try:
        main(sys.argv[1], sys.argv[2], "--require-cfg" in args,
             "--require-cet" in args)
    except (ValueError, OSError) as error:
        raise SystemExit(f"OPENATL_PE_MITIGATIONS_FAIL: {error}")
