#!/usr/bin/env python3
"""Fail closed on missing/replaced repository WTL, legacy targets and CI gates."""
from __future__ import annotations
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests" / "wtl10"))
from manifest_contract import verify_repository_wtl_stamp  # noqa: E402

def main() -> int:
    fail: list[str] = []
    tracked = set(subprocess.check_output(
        ["git", "-C", str(ROOT), "ls-files", "-z"], timeout=30
    ).decode().split("\0"))
    try:
        verify_repository_wtl_stamp()
    except (ValueError, OSError, KeyError) as error:
        fail.append("source+resource integrity: " + str(error))
    if "vcpkg.json" in tracked or (ROOT / "vcpkg.json").exists():
        fail.append("WTL must not be installed from vcpkg")
    cmake = (ROOT / "CMakeLists.txt").read_text("utf-8")
    config = (ROOT / "cmake/OpenATLWTL.cmake").read_text("utf-8")
    for token in (
        "add_library(openatl_atl INTERFACE)",
        "add_library(openwtl_wtl INTERFACE)",
        "add_library(OpenWTL::WTL ALIAS openwtl_wtl)",
        "target_link_libraries(openwtl_wtl INTERFACE openatl_atl)",
        "add_subdirectory(tests/com_server)",
    ):
        if token not in cmake:
            fail.append("required backend/frontend target contract absent: " + token)
    if "OpenATL::ATL" in cmake or "add_library(OpenATL::ATL" in cmake:
        fail.append("obsolete public target OpenATL::ATL must not remain")
    for token in (
        'set(_wtl_root "${CMAKE_CURRENT_SOURCE_DIR}/wtl")',
        'target_include_directories(openwtl_wtl BEFORE INTERFACE "${_wtl_source}")',
        'set(_openatl_wtl_samples "${_wtl_root}/Samples")',
        'set(_openatl_wtl_original_include "${_wtl_source}")',
        "MS-PL.txt", "AppWizard/Setup.js",
    ):
        if token not in config:
            fail.append("local WTL target/source contract absent: " + token)
    for forbidden in (
        "VCPKG_INSTALLED_DIR", "VCPKG_OVERLAY", "vcpkg-cache",
        "file(DOWNLOAD", "file(COPY", "FetchContent",
        "PrepareWtl.cmake", "downloads.sourceforge.net",
    ):
        if forbidden in config:
            fail.append("alternate WTL source or adapter in CMake: " + forbidden)
    for path in ("tests/atl_host/CMakeLists.txt",
                 "tests/com_server/CMakeLists.txt",
                 "tests/official_samples/CMakeLists.txt",
                 "tests/ratl_samples/CMakeLists.txt",
                 "tests/wtl_unmodified/CMakeLists.txt"):
        text = (ROOT / path).read_text("utf-8")
        if "OpenATL::ATL" in text or "OpenWTL::WTL" not in text:
            fail.append("legacy target or missing public target in " + path)
    wtl = ROOT / "wtl"
    for sample in ("Aero", "Alpha", "BmpView", "GuidGen", "MDIDocVw",
                   "MemDlg", "MTPad", "MTPad7", "TabBrowser",
                   "WTLExplorer", "Wizard97Test"):
        if not (wtl / "Samples" / sample).is_dir():
            fail.append("original WTL sample omitted: " + sample)
    for required in ("wtl/MS-PL.txt", "wtl/SOURCE.md",
                     "wtl/Include/atlapp.h", "wtl/Include/atlmisc.h",
                     "wtl/Include/atlctrls.h", "wtl/AppWizard/Setup.js"):
        if required not in tracked:
            fail.append("required vendored release file untracked: " + required)
    workflow = (ROOT / ".github/workflows/openatl-qualification.yml").read_text("utf-8")
    for forbidden in (
        "repository: qigao/vcpkg-cache", "setup-vcpkg-curl-cache.py",
        "OPENATL_WTL_VCPKG_INSTALLED_DIR", "qigao-wtl10-headers",
        "Install pinned WTL10 from the selected vcpkg overlay",
    ):
        if forbidden in workflow:
            fail.append("CI still uses external WTL provider: " + forbidden)
    for marker in (
        "OPENATL_WTL_FULL_ORIGINAL_MSVC_X64_OK",
        "OPENATL_WTL_FULL_ORIGINAL_LLVM_X64_OK",
        "openatl-msvc-arm64-original",
        "openatl-llvm-arm64-original",
        "OPENATL_WTL_ORIGINAL_ARM64_MSVC_BUILD_OK",
        "OPENATL_WTL_ORIGINAL_ARM64_LLVM_BUILD_OK",
        "Native ARM64 WTL10 full-original llvm-mingw CTest equivalents x5",
        "Native ARM64 WTL10 full-original MSVC CTest equivalents x5",
        "tests/wtl_unmodified/audit_wtl_api.py",
        "python3 tests/wtl_dependency_policy.py",
    ):
        if marker not in workflow:
            fail.append("WTL original native gate removed: " + marker)
    exact_ref = "ref: ${{ github.event.pull_request.head.sha || github.sha }}"
    if workflow.count(exact_ref) != 5:
        fail.append("source + both Windows compiler producers and real ARM64 runners must pin exact HEAD")
    if workflow.count("-DOPENATL_BUILD_WTL_UNMODIFIED=ON") < 5:
        fail.append("WTL original native tests must remain on both x64/ARM64 compiler paths")
    # Only target Windows x64/ARM64. Linux is permitted for the small source
    # integrity job, but must not compile a separate platform matrix.
    for stale in ("msvc-win32-official:", "linux-macos-cross:",
                  "llvm-i686", "msvc-x86", "msvc-salts",
                  "Published Salts Unicode SDK"):
        if stale in workflow:
            fail.append("obsolete CI toolchain/target: " + stale)
    for job in ("source-tree-only", "windows-llvm", "msvc-windows",
                "windows-arm64-llvm", "windows-arm64-msvc"):
        if workflow.count("  " + job + ":") != 1:
            fail.append("required Windows x64/ARM64 job missing or duplicated: " + job)
    if ("- target: x64" not in workflow or "- target: arm64" not in workflow or
            "vc_arch: amd64" not in workflow or "vc_arch: amd64_arm64" not in workflow):
        fail.append("MSVC Windows x64/ARM64 matrix must remain explicit")
    presets = json.loads((ROOT / "CMakeUserPresets.json").read_text("utf-8"))
    for kind in ("configurePresets", "buildPresets", "testPresets"):
        actual = {p["name"] for p in presets.get(kind, [])}
        if actual != {"llvm-x86_64", "llvm-aarch64", "msvc-x64", "msvc-arm64"}:
            fail.append("only x64/ARM64 MSVC and llvm-mingw profiles allowed: " + kind)
    cmake_src = (ROOT / "CMakeLists.txt").read_text("utf-8")
    unicode_header = (ROOT / "alt/include/openatl/unicode.h").read_text("utf-8")
    for forbidden in ("find_package(Salts", "Salts::Unicode", "SALTS_ROOT",
                      "option(OPENATL_USE_SALTS_UNICODE"):
        if forbidden in cmake_src:
            fail.append("Salts Unicode SDK dependency must be retired: " + forbidden)
    if "#include <salts_" in unicode_header or "SALTS_UNICODE_" in unicode_header:
        fail.append("public Unicode facade must use native Windows encoding only")
    unicode_source = (ROOT / "alt/include/openatl/text_encoding.h").read_text("utf-8")
    for native in ("MultiByteToWideChar", "WideCharToMultiByte", "GetACP"):
        if native not in unicode_source:
            fail.append("required strict Win32 encoding API missing: " + native)
    native_arm64 = (ROOT / "tests/wtl10/native_arm64.py").read_text("utf-8")
    for marker in ("openatl_wtl_unmodified_headers",
                   "OPENATL_WTL_ORIGINAL_MAP_OK",
                   "OPENATL_WTL_START_MONITOR_PROCESS_OK"):
        if marker not in native_arm64:
            fail.append("ARM64 native behavior removed: " + marker)
    for path in ("tests/wtl_unmodified/server_monitor.cpp",
                 "tests/wtl_unmodified/reg_property.cpp",
                 "tests/wtl_unmodified/hyperlink_registry.cpp",
                 "tests/wtl_unmodified/print_defaults.cpp"):
        if path not in tracked:
            fail.append("real original WTL behavior test omitted: " + path)
    if fail:
        for error in fail:
            print("OPENWTL_SOURCE_POLICY_RED " + error, file=sys.stderr)
        return 1
    print("OPENWTL_SOURCE_POLICY_OK version=10.0.10320 files=296 "
          "provider=repo public=OpenWTL::WTL backend=internal "
          "original_samples_preserved=11 native_matrix=msvc-llvm-x64-arm64")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
