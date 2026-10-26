#!/usr/bin/env python3
"""Fail closed if the OpenATL repository acquires an installed SDK path.

OpenATL's CMake targets are source-tree-only INTERFACE targets. This checks
tracked CMake files, avoiding downloaded WTL/test/toolchain dependencies.
The operating system's Windows SDK is unrelated and remains allowed.
"""
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
TRACKED = subprocess.check_output(
    ["git", "-C", str(ROOT), "ls-files", "-z"], timeout=30
).decode("utf-8").split("\x00")
CMAKE_FILES = [
    Path(item) for item in TRACKED if item and (
        item.endswith("/CMakeLists.txt")
        or item == "CMakeLists.txt"
        or item.lower().endswith((".cmake", ".cmake.in"))
    )
]
RULES = [
    ("install/export commands", re.compile(r"^\s*(?:install|export)\s*\(", re.I | re.M)),
    ("CPack registration", re.compile(r"^\s*include\s*\(\s*['\"]?CPack\b", re.I | re.M)),
    ("package config generation", re.compile(
        r"^\s*(?:configure_package_config_file|"
        r"write_basic_package_version_file|export_package_dependencies)\s*\(",
        re.I | re.M,
    )),
    ("install/package build targets", re.compile(
        r"^\s*add_custom_target\s*\(\s*['\"]?(?:install|package|sdk)\b",
        re.I | re.M,
    )),
]
FORBIDDEN_FILENAMES = {
    "openatlconfig.cmake", "openatlconfigversion.cmake",
    "openatltargets.cmake", "cpackconfig.cmake",
    "cpacksourceconfig.cmake",
}

def check() -> int:
    if not CMAKE_FILES:
        print("FAIL: no tracked OpenATL CMake files found", file=sys.stderr)
        return 1
    violations: list[str] = []
    for tracked in TRACKED:
        if not tracked:
            continue
        if Path(tracked).name.lower() in FORBIDDEN_FILENAMES:
            violations.append(f"{tracked}: installed OpenATL package file")
    for relative in CMAKE_FILES:
        content = (ROOT / relative).read_text(encoding="utf-8")
        for description, pattern in RULES:
            match = pattern.search(content)
            if match:
                line = content.count("\n", 0, match.start()) + 1
                violations.append(f"{relative}:{line}: {description}")
    primary = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    if 'set(_openatl_atl_include "${CMAKE_CURRENT_SOURCE_DIR}/include")' not in primary:
        violations.append("CMakeLists.txt: missing fixed source-tree ATL include path")
    if 'if(DEFINED OPENATL_ATL_INCLUDE_DIR)' not in primary:
        violations.append("CMakeLists.txt: missing external ATL override rejection")
    if violations:
        for item in violations:
            print("OPENATL_SOURCE_ONLY_FAIL " + item, file=sys.stderr)
        return 1
    print(f"OPENATL_SOURCE_TREE_ONLY_OK cmake_files={len(CMAKE_FILES)} "
          "install=0 export=0 package_config=0")
    return 0

if __name__ == "__main__":
    raise SystemExit(check())
