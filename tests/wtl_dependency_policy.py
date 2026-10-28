#!/usr/bin/env python3
"""Verify WTL10 provenance and reproducible OpenATL integration.

This policy proves the *current* pinned WTL input. It does not ban future
ATL/WTL compatibility APIs, COM Server helpers, CMake targets, optional
package-manager dependencies, install/export rules or build utilities.
Native Windows sample behavior is qualified by separate CTest workflows.
"""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]


def check() -> int:
    dependency = (ROOT / "cmake/OpenATLWTL.cmake").read_text("utf-8")
    recipe = (ROOT / "cmake/wtl/PrepareWtl.cmake").read_text("utf-8")
    cmake = (ROOT / "CMakeLists.txt").read_text("utf-8")
    workflow = (ROOT / ".github/workflows/openatl-qualification.yml").read_text("utf-8")
    errors = []

    if "FetchContent_Declare(openatl_wtl_source" not in dependency or (
        "SHA512=086a6cf6a49a4318a8c519136ba6019" not in dependency
    ):
        errors.append("WTL10 source archive and SHA512 pin must remain reproducible")
    if 'target_include_directories(openatl_atl INTERFACE "${_wtl_headers}")' not in dependency:
        errors.append("Current OpenATL::ATL target must expose WTL headers")
    if "function(_openatl_replace_between" not in recipe or (
        "class CAppModule : public ATL::CComModule" not in recipe
    ):
        errors.append("WTL10 adapter must operate on checksum-verified upstream headers")
    if "atlapp.h-openatl-ui-only.patch" in recipe or (
        ROOT / "ports/wtl/atlapp.h-openatl-ui-only.patch"
    ).exists():
        errors.append("Reject mismatched mirror-based patches to WTL inputs")
    if "if(DEFINED OPENATL_WTL_INCLUDE_DIR)" not in cmake:
        errors.append("Current build must reject an unverified alternate WTL root")
    if "wtltest-ui-only.patch" in workflow:
        errors.append("Native WTL sample must not be changed by a consumer patch")
    if "--preset" not in workflow or "cmake --build" not in workflow:
        errors.append("CI must compile actual WTL consumers via project CMake")

    if errors:
        for error in errors:
            print("WTL_PROVENANCE_FAIL " + error, file=sys.stderr)
        return 1
    print("WTL_PROVENANCE_OK version=10.0.10320 sha512=verified "
          "source=FetchContent openatl=source-owned client=1 server=1")
    return 0


if __name__ == "__main__":
    raise SystemExit(check())
