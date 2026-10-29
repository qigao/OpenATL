#!/usr/bin/env python3
"""Verify repository-owned ATL inputs; do not forbid future installed builds.

OpenATL currently builds its own headers as source-tree INTERFACE targets.
Packaging, install() and export() may be added after a separate design and
native consumer qualification. This gate protects the present source owner.
"""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def check() -> int:
    tracked = set(subprocess.check_output(
        ["git", "-C", str(ROOT), "ls-files", "-z"], timeout=30
    ).decode("utf-8").split("\x00"))
    required = [
        "CMakeLists.txt", "include/atlbase.h", "include/atlwin.h",
        "include/atlcomcli.h", "include/atlcom.h",
        "include/openatl/com_server.h", "include/openatl/com_registration.h",
        "tests/com_server/CMakeLists.txt",
        "third_party/wtl/WTL10_10320_Final.zip",
        "third_party/wtl/README.md",
    ]
    errors = [f"Missing repository-owned ATL source: {item}"
              for item in required
              if item not in tracked or not (ROOT / item).is_file()]

    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    expected = (
        'set(_openatl_atl_include "${CMAKE_CURRENT_SOURCE_DIR}/include")',
        "add_library(OpenATL::ATL ALIAS openatl_atl)",
        "add_subdirectory(tests/com_server)",
    )
    for marker in expected:
        if marker not in cmake:
            errors.append(f"CMakeLists.txt missing ATL source/server: {marker}")

    if errors:
        for error in errors:
            print("OPENATL_SOURCE_PROVENANCE_FAIL " + error, file=sys.stderr)
        return 1
    print("OPENATL_SOURCE_PROVENANCE_OK "
          "atl=source-owned com_client=1 com_server=1 "
          "build=source-tree packaging=unqualified")
    return 0


if __name__ == "__main__":
    raise SystemExit(check())
