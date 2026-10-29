#!/usr/bin/env python3
"""Fail closed if the original Microsoft COMMap IDL differs by one byte."""
from pathlib import Path
import hashlib
import sys

SOURCE_SHA = "dcf19ff69001c6f2f242d15c917a5598b03d70ba"
if len(sys.argv) != 2:
    raise SystemExit("usage: check_idl.py COMMap.idl")
data = Path(sys.argv[1]).read_bytes()
identity = hashlib.sha1(
    b"blob " + str(len(data)).encode("ascii") + b"\0" + data
).hexdigest()
if identity != SOURCE_SHA:
    raise SystemExit(f"MICROSOFT_COMMAP_IDL_ORIGINAL_FAIL {identity} != {SOURCE_SHA}")
print(f"MICROSOFT_COMMAP_IDL_ORIGINAL_OK blob={identity} bytes={len(data)}")
