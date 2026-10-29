"""Fail-closed provenance contract for independent original WTL ARM64 builds.

Legacy (non-original) artifacts keep their existing flat SHA256 inventory.
Only original artifacts use the versioned provenance object. This intentionally
binds real native execution to the checked-out code and pinned WTL producer.
"""
from __future__ import annotations

import json
import os
from pathlib import Path
import re
import subprocess

WTL_VERSION = "10.0.10320#7"
WTL_PROVIDER = "vcpkg-cache/ports/wtl"
WTL_ARCHIVE_SHA512 = (
    "086a6cf6a49a4318a8c519136ba6019ded7aa7f2c1d85f78c30b21183654537"
    "b3428a400a64fcdacba3a7a10a9ef05137b6f2119f59594da300d55f9ebfb1309"
)
WTL_STAMP = (
    f"wtl={WTL_VERSION}\nsha512={WTL_ARCHIVE_SHA512}\nprovider={WTL_PROVIDER}\n"
)
_SHA256 = re.compile(r"[0-9a-f]{64}")
_SHA = re.compile(r"[0-9a-f]{40}")
_TRIPLET = re.compile(r"[A-Za-z0-9_.+-]+")


def current_checkout_sha() -> str:
    # Always measure the actual working tree. On pull_request workflows,
    # GITHUB_SHA designates GitHub's synthetic merge commit, not the head
    # being qualified; relying on it makes provenance silently incorrect.
    sha = subprocess.check_output(
        ["git", "rev-parse", "HEAD"],
        cwd=Path(__file__).resolve().parents[2],
        text=True, timeout=10,
    ).strip()
    if not _SHA.fullmatch(sha):
        raise ValueError("checkout SHA must be 40 lowercase hex digits")
    expected = os.environ.get("OPENATL_EXPECTED_CHECKOUT_SHA", "")
    if os.environ.get("GITHUB_ACTIONS") == "true" and not expected:
        raise ValueError("exact PR/push checkout SHA missing in CI")
    if expected and (not _SHA.fullmatch(expected) or sha != expected):
        raise ValueError("actual checkout differs from exact PR/push SHA")
    return sha


def verify_installed_wtl_stamp(cmake_cache: str) -> None:
    """Read the actual installed port stamp, never just assert a constant."""
    def cache_entry(name: str) -> str:
        for line in cmake_cache.splitlines():
            if line.startswith(name + ":"):
                return line.partition("=")[2].strip()
        return ""

    root = (os.environ.get("OPENATL_WTL_VCPKG_INSTALLED_DIR")
            or cache_entry("VCPKG_INSTALLED_DIR")
            or os.environ.get("VCPKG_INSTALLED_DIR"))
    triplet = (os.environ.get("OPENATL_WTL_VCPKG_TRIPLET")
               or cache_entry("VCPKG_TARGET_TRIPLET"))
    if not root or not triplet or not _TRIPLET.fullmatch(triplet):
        raise ValueError("original WTL install root/triplet not resolved")
    stamp_path = Path(root) / triplet / "share" / "wtl" / "openatl-wtl-source.txt"
    if stamp_path.read_text("utf-8").replace("\r\n", "\n") != WTL_STAMP:
        raise ValueError("original WTL source stamp mismatches pinned provider")


def _provenance(toolchain: str, checkout_sha: str) -> dict[str, object]:
    if toolchain not in ("llvm", "msvc"):
        raise ValueError("unsupported original ARM64 toolchain")
    if not isinstance(checkout_sha, str) or not _SHA.fullmatch(checkout_sha):
        raise ValueError("invalid original ARM64 checkout SHA")
    return {
        "schema": 1,
        "profile": "openatl-wtl-original-arm64",
        "toolchain": toolchain,
        "checkout_sha": checkout_sha,
        "wtl_version": WTL_VERSION,
        "wtl_provider": WTL_PROVIDER,
        "wtl_archive_sha512": WTL_ARCHIVE_SHA512,
    }


def make_original_manifest(files: dict[str, str], toolchain: str,
                           checkout_sha: str) -> dict[str, object]:
    if not isinstance(files, dict) or any(
        not isinstance(name, str) or not isinstance(digest, str)
        or not _SHA256.fullmatch(digest) for name, digest in files.items()
    ):
        raise ValueError("invalid original ARM64 SHA256 inventory")
    return {"provenance": _provenance(toolchain, checkout_sha),
            "files": dict(files)}


def read_manifest(path: Path) -> object:
    def unique_keys(pairs: list[tuple[str, object]]) -> dict[str, object]:
        result: dict[str, object] = {}
        for key, value in pairs:
            if key in result:
                raise ValueError(f"duplicate ARM64 manifest key: {key}")
            result[key] = value
        return result
    return json.loads(path.read_text("utf-8-sig"), object_pairs_hook=unique_keys)


def verify_manifest(payload: object, required: set[str], toolchain: str,
                    checkout_sha: str | None = None, *,
                    original: bool = False) -> dict[str, str]:
    if not isinstance(payload, dict):
        raise ValueError("ARM64 manifest must be an object")
    if original:
        if set(payload) != {"provenance", "files"}:
            raise ValueError("original ARM64 provenance object is missing or extended")
        actual = payload["provenance"]
        if not isinstance(actual, dict) or type(actual.get("schema")) is not int:
            raise ValueError("original ARM64 provenance schema must be an integer")
        if actual != _provenance(toolchain, checkout_sha):
            raise ValueError("original ARM64 WTL/toolchain/checkout provenance mismatch")
        files = payload["files"]
    else:
        files = payload
    if not isinstance(files, dict) or set(files) != required:
        raise ValueError("ARM64 provenance set mismatch")
    for name, digest in files.items():
        if not isinstance(digest, str) or not _SHA256.fullmatch(digest):
            raise ValueError(f"invalid ARM64 SHA256 for {name}")
    return files
