#!/usr/bin/env python3
"""OpenATL uses in-tree ATL + CMake-managed official WTL; never MSVC ATL.

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
    recipe = (ROOT / "cmake/wtl/PrepareWtl.cmake").read_text("utf-8")
    dependency = (ROOT / "cmake/OpenATLWTL.cmake").read_text("utf-8")
    cmake = (ROOT / "CMakeLists.txt").read_text("utf-8")
    errors = []
    if manifest.get("dependencies") or config.get("overlay-ports"):
        errors.append("Downstream WTL/ATL must not need vcpkg dependencies or overlays")
    if "FetchContent_Declare(openatl_wtl_source" not in dependency or (
            "SHA512=086a6cf6a49a4318a8c519136ba6019" not in dependency):
        errors.append("Managed WTL must retain its pinned SourceForge archive/hash")
    if 'target_include_directories(openatl_atl INTERFACE "${_wtl_headers}")' not in dependency:
        errors.append("OpenATL::ATL must supply adapted WTL headers transitively")
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
        errors.append("WTL adapter must remove registry hyperlink and COM print callbacks")
    if ("Preserve MRU menu operations and per-user registry persistence" not in recipe or
            "ReadFromRegistry" not in recipe or
            "WriteToRegistry" not in recipe or
            "CRecentDocumentListBase" not in recipe or
            "CFindFile" not in recipe or
            "registry-only CRegProperty module" not in recipe or
            "AtlGetStockPen" not in recipe):
        errors.append("WTL MRU registry persistence and normal menu/file utilities must be retained")
    if re.search(r"(?i)OpenATL::WTL|add_library\s*\(\s*openatl_wtl\b", cmake):
        errors.append("OpenATL must not synthesize a WTL target")
    if "if(DEFINED OPENATL_WTL_INCLUDE_DIR)" not in cmake:
        errors.append("CMake must reject manually supplied WTL roots")
    workflow = (ROOT / ".github/workflows/openatl-qualification.yml").read_text("utf-8")
    if "tools/vcpkg" in workflow or "--overlay-ports" in workflow:
        errors.append("CI must use the same automatic dependency path as downstream")
    if "wtltest-ui-only.patch" in workflow:
        errors.append("The standard WTL SDI sample must compile without downstream source patches")
    if "--preset" not in workflow or "cmake --build" not in workflow:
        errors.append("CI must configure and build through the project presets")
    if "scripts/" in workflow or (ROOT / "scripts").exists():
        errors.append("Legacy scripts/ directory or workflow calls must not return")
    if "WTL10_10320_Final.zip" in workflow or "WTL10_10320_Release.zip" in workflow:
        errors.append("GitHub Actions must not manually download WTL ZIP")
    if errors:
        for error in errors:
            print("WTL_DEPENDENCY_POLICY_FAIL " + error, file=sys.stderr)
        return 1
    print("WTL_DEPENDENCY_POLICY_OK version=10.0.10320 source=FetchContent "
          "downstream_overlay=0 microsoft_atl=0 single_target=1")
    return 0

if __name__ == "__main__":
    raise SystemExit(check())
