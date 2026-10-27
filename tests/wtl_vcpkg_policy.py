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
    if port.get("port-version") != 10:
        errors.append("WTL UI-only overlay must use exact #10")
    if "atlapp.h-openatl-ui-only.patch" in recipe or (
            ROOT / "ports/wtl/atlapp.h-openatl-ui-only.patch").exists():
        errors.append("Mismatched mirror-based WTL UI patch is forbidden")
    if ("function(_openatl_replace_between" not in recipe or
            "class CAppModule : public ATL::CComModule" not in recipe or
            "HRESULT Init(HINSTANCE hInstance)" not in recipe):
        errors.append("WTL must transform the checked SourceForge UI module")
    if ("CServerAppModule - module class for a COM server application" not in recipe
            or "ATL::_pAtlModule->m_csStaticDataInitAndTypeInfo" not in recipe):
        errors.append("Missing Win32 lock or Server deletion anchors")
    if ("legacy hyperlink registry color access" not in recipe or
            "COM callback-based PrintDlgEx" not in recipe or
            "CPageSetupDialogImpl - Page Setup dialog" not in recipe):
        errors.append("WTL overlay must remove registry hyperlink and COM print callbacks")
    if ("MRU registry persistence" not in recipe or
            "class CRecentDocumentListBase" not in recipe or
            "class CFindFile" not in recipe):
        errors.append("WTL MRU persistence deletion must preserve normal menu/file utilities")
    sample_patch = ROOT / "tests/wtl10/wtltest-ui-only.patch"
    if not sample_patch.exists() or (
            "-    hRes = _Module.Init(NULL, hInstance);" not in
            sample_patch.read_text("utf-8") or
            "-#include <atlcom.h>" not in
            sample_patch.read_text("utf-8")):
        errors.append("Pinned SDI must call Init(hInstance) without COM Server header")
    if re.search(r"(?i)OpenATL::WTL|add_library\s*\(\s*openatl_wtl\b", cmake):
        errors.append("OpenATL must not synthesize a WTL target")
    if ("VCPKG_TARGET_TRIPLET" not in cmake or
            "vcpkg_installed" not in cmake):
        errors.append("CMake must consume manifest-installed WTL")
    if "if(DEFINED OPENATL_WTL_INCLUDE_DIR)" not in cmake:
        errors.append("CMake must reject manually supplied WTL roots")
    workflow = (ROOT / ".github/workflows/openatl-qualification.yml").read_text("utf-8")
    if "vcpkg install" not in workflow and "vcpkg.exe" not in workflow and "vcpkg/vcpkg" not in workflow:
        errors.append("GitHub Actions must install WTL through vcpkg manifest")
    if "wtltest-ui-only.patch" not in workflow:
        errors.append("CI must apply exactly the pinned UI-only SDI test patch")
    if "--x-install-root" not in workflow or "cmake --build" not in workflow:
        errors.append("GitHub Actions must isolate target triplets and build with CMake")
    if "scripts/" in workflow or (ROOT / "scripts").exists():
        errors.append("Legacy scripts/ directory or workflow calls must not return")
    if "WTL10_10320_Final.zip" in workflow or "WTL10_10320_Release.zip" in workflow:
        errors.append("GitHub Actions must not manually download WTL ZIP")
    if errors:
        for error in errors:
            print("WTL_VCPKG_POLICY_FAIL " + error, file=sys.stderr)
        return 1
    print("WTL_VCPKG_POLICY_OK version=10.0.10320 source=vcpkg "
          "overlay=1 microsoft_atl=0 invented_wtl_target=0")
    return 0

if __name__ == "__main__":
    raise SystemExit(check())
