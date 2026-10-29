#!/usr/bin/env python3
"""Fail-closed ARM64 PE provenance for a second, independent original WTL build.

The existing adapted ARM64 artifacts and manifests remain untouched. Native
Windows ARM64 CI uses native_arm64.py on BOTH artifact sets, five repetitions.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess
import sys

from native_arm64 import TARGETS
from manifest_contract import (current_checkout_sha, make_original_manifest,
                               verify_installed_wtl_stamp)


def main() -> int:
    if len(sys.argv) != 3 or sys.argv[2] not in ("llvm", "msvc"):
        raise SystemExit("Usage: original_arm64_manifest.py BUILD_DIR llvm|msvc")
    root = Path(sys.argv[1]).resolve(strict=True)
    if not root.name.endswith("-original"):
        raise SystemExit("Refuse a non-original ARM64 build directory")
    cache_file = root / "CMakeCache.txt"
    cache = cache_file.read_text("utf-8")
    if ("OPENATL_WTL_FULL_ORIGINAL:BOOL=ON" not in cache
            or "OPENATL_BUILD_WTL_UNMODIFIED:BOOL=ON" not in cache):
        raise SystemExit("Original WTL qualification CMake options are not both ON")
    # Bind the provenance to the actual installed WTL port identity.
    verify_installed_wtl_stamp(cache)
    if (root / "wtl/include/atlapp.h").exists():
        raise SystemExit("Build-local adapted WTL header found in zero-adapter mode")

    samples = ("guidgen", "memdlg") if sys.argv[2] == "msvc" else ("guidgen",)
    exe_targets = (*TARGETS, *("openatl_official_" + name for name in samples))
    expected = {name + ".exe" for name in exe_targets}
    expected.update(("openatl_unload_fixture.dll", "openatl_unload_probe.dll",
                     "openatl_com_server_fixture.dll"))
    validator = Path(__file__).with_name("verify_pe_machine.py")
    inventory: dict[str, str] = {}
    for name in sorted(expected):
        path = root / name
        if not path.is_file() or path.stat().st_size == 0:
            raise SystemExit("Missing original WTL ARM64 PE: " + name)
        args = [sys.executable, str(validator), str(path), "aarch64"]
        if (name in ("openatl_gui_runtime.exe", "openatl_unicode_utf8.exe",
                    "openatl_wtl10_sdi.exe") or name.startswith("openatl_official_")):
            args.append("--require-resources")
        subprocess.run(args, check=True, timeout=20)
        inventory[name] = hashlib.sha256(path.read_bytes()).hexdigest()

    payload = json.dumps(make_original_manifest(inventory, sys.argv[2],
                current_checkout_sha()), sort_keys=True, indent=2) + "\n"
    temporary = root / "manifest.json.tmp"
    temporary.write_text(payload, encoding="utf-8")
    temporary.replace(root / "manifest.json")
    print(f"OPENATL_WTL_ORIGINAL_ARM64_PE_OK toolchain={sys.argv[2]} "
          f"files={len(expected)} sha256=1 machine=1 original=1 adapter=0 "
          "source_stamp=1 checkout=1")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
