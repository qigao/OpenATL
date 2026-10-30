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
WTL_PROVIDER = "OpenWTL::WTL"
WTL_ARCHIVE_SHA512 = (
    "086a6cf6a49a4318a8c519136ba6019ded7aa7f2c1d85f78c30b21183654537"
    "b3428a400a64fcdacba3a7a10a9ef05137b6f2119f59594da300d55f9ebfb1309"
)
WTL_SOURCE_ROOT = Path(__file__).resolve().parents[2] / "wtl"
_SHA256 = re.compile(r"[0-9a-f]{64}")
_SHA = re.compile(r"[0-9a-f]{40}")


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


def verify_repository_wtl_stamp(root: Path | None = None) -> None:
    """Verify exact Git blobs plus the normalized clean checkout on every OS."""
    repository = Path(__file__).resolve().parents[2]
    root = Path(root) if root is not None else repository / "wtl"
    manifest = json.loads((root / "source-manifest.json").read_text("utf-8"))
    if (manifest.get("schema") != 1 or manifest.get("name") != "WTL" or
            manifest.get("version") != "10.0.10320" or
            manifest.get("archive_sha512") != WTL_ARCHIVE_SHA512 or
            manifest.get("patch_repository") != "qigao/vcpkg-cache" or
            manifest.get("patch_commit") !=
            "ddb500fb7c1472107efbd79ea92d7552cb01f452"):
        raise ValueError("vendored WTL source, patch or release provenance mismatch")
    files = manifest.get("files")
    if not isinstance(files, dict) or len(files) != 296:
        raise ValueError("vendored WTL source inventory is incomplete")
    if set(manifest.get("changed_paths", [])) != {
            "AppWizard/Setup.js", "Include/atlctrls.h",
            "Include/atlctrlw.h", "Include/atlctrlx.h",
            "Include/atlmisc.h", "Include/atlribbon.h"}:
        raise ValueError("WTL patch inventory has drifted")

    # Git checkout can use core.autocrlf on native Windows. Hash the original
    # committed blob identities, then independently require a clean checkout
    # using Git's actual text/attribute normalization. Raw working-file SHA1
    # would falsely reject pristine CRLF files.
    listing = subprocess.check_output(
        ["git", "-C", str(repository), "ls-tree", "-r", "-z", "HEAD", "--", "wtl"],
        timeout=15,
    )
    git_blobs: dict[str, str] = {}
    for entry in listing.split(b"\x00"):
        if not entry:
            continue
        metadata, filename = entry.split(b"\t", 1)
        mode, kind, digest = metadata.split(b" ")
        if kind != b"blob":
            raise ValueError("WTL source tree contains a non-blob entry")
        full_path = filename.decode("utf-8")
        if not full_path.startswith("wtl/"):
            raise ValueError("WTL source tree path escaped repository")
        git_blobs[full_path[4:]] = digest.decode("ascii")
    allowed = set(files) | {"SOURCE.md", "source-manifest.json"}
    if set(git_blobs) != allowed:
        raise ValueError("committed WTL source inventory is incomplete or enlarged")
    for relative, expected in files.items():
        if (not isinstance(relative, str) or
                not isinstance(expected, str) or not _SHA.fullmatch(expected) or
                relative.startswith("/") or ".." in Path(relative).parts):
            raise ValueError("invalid WTL file manifest entry")
        if git_blobs.get(relative) != expected or not (root / relative).is_file():
            raise ValueError(f"vendored WTL Git blob differs: {relative}")
    actual = {p.relative_to(root).as_posix() for p in root.rglob("*")
              if p.is_file()}
    if actual != allowed:
        raise ValueError("WTL checkout missing files or contains extra source")
    clean = subprocess.run(
        ["git", "-C", str(repository), "diff", "--quiet", "HEAD", "--", "wtl"],
        timeout=15, check=False,
    )
    if clean.returncode != 0:
        raise ValueError("WTL worktree is not identical to committed source")


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
