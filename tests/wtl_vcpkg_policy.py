#!/usr/bin/env python3
"""OpenATL uses in-tree ATL + vcpkg-installed official WTL; never MSVC ATL.

This is source/build policy only. CI compiles and executes the actual WTL
headers separately; file inspection is not a substitute for those tests.
"""
from pathlib import Path
import json
import re
import sys

ROOT = Path(__file__).resolve().parents[1]

def check() -> int:
    manifest = json.loads((ROOT / "vcpkg.json").read_text("utf-8"))
    config = json.loads((ROOT / "vcpkg-configuration.json").read_text("utf-8"))
    port = json.loads((ROOT / "ports/wtl/vcpkg.json").read_text("utf-8"))
    recipe = (ROOT / "ports/wtl/portfile.cmake").read_text("utf-8")
    cmake = (ROOT / "CMakeLists.txt").read_text("utf-8")
    errors = []
    if "wtl" not in manifest.get("dependencies", []):
        errors.append("vcpkg.json must declare wtl")
    if "atl" in manifest.get("dependencies", []):
        errors.append("OpenATL must not depend on vcpkg Microsoft atl")
    if "./ports" not in config.get("overlay-ports", []):
        errors.append("vcpkg overlay-ports must select checked-in ports/wtl")
    if port.get("name") != "wtl" or port.get("version") != "10.0.10320":
        errors.append("WTL overlay must identify official WTL10")
    if port.get("dependencies"):
        errors.append("WTL overlay must not require Microsoft ATL or another provider")
    overrides = manifest.get("overrides", [])
    if not any(o.get("name") == "wtl" and
               o.get("version") == port.get("version") and
               o.get("port-version") == port.get("port-version")
               for o in overrides):
        errors.append("vcpkg.json must pin exact WTL overlay revision")
    if ("vcpkg_from_sourceforge(" not in recipe or
            "086a6cf6a49a4318a8c519136ba6019" not in recipe):
        errors.append("WTL vcpkg overlay must retain pinned upstream recipe")
    if re.search(r"(?i)OpenATL::WTL|add_library\s*\(\s*openatl_wtl\b", cmake):
        errors.append("OpenATL must not synthesize a WTL target")
    if ("VCPKG_TARGET_TRIPLET" not in cmake or
            "vcpkg_installed" not in cmake):
        errors.append("CMake must consume manifest-installed WTL")
    if "if(DEFINED OPENATL_WTL_INCLUDE_DIR)" not in cmake:
        errors.append("CMake must reject manually supplied WTL roots")
    for name in ["scripts/windows-openatl-bootstrap.ps1",
                 "scripts/wtl10-cross-host.sh"]:
        src = (ROOT / name).read_text("utf-8")
        if "vcpkg" not in src or "install" not in src:
            errors.append(f"{name} must invoke manifest WTL install")
        if ("WTL10_10320_Final.zip" in src or
                "WTL10_10320_Release.zip" in src):
            errors.append(f"{name} must not manually download WTL ZIP")
    if errors:
        for error in errors:
            print("WTL_VCPKG_POLICY_FAIL " + error, file=sys.stderr)
        return 1
    print("WTL_VCPKG_POLICY_OK version=10.0.10320 source=vcpkg "
          "overlay=1 microsoft_atl=0 invented_wtl_target=0")
    return 0

if __name__ == "__main__":
    raise SystemExit(check())
