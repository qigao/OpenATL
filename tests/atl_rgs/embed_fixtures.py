#!/usr/bin/env python3
"""Embed unchanged, Git-SHA-pinned Microsoft ATL RGS fixtures as C++ literals.

This is part of test compilation only: it never parses a registry, runs
MIDL/ATL, invokes a shell, or mutates third-party source files.
"""
from pathlib import Path
import hashlib
import sys

EXPECTED_GIT_BLOBS = {
    "Chain.rgs": "30525e92df26595e9bc00015ea970b3ea5d97e04",
    "Outer.rgs": "0ff53ea6aa644a7b96ec6a6f77dc8ab3f1680285",
    "ArrayManager.rgs": "39bf8a7d2bc75c21f81c74b761ce97c95234c59a",
    "MapCollection.rgs": "b81d8194a2c9020de381c8e9f90eb01b6fa6428f",
}

def make(output: Path, directory: Path) -> None:
    lines = [
        "// Auto-generated from byte-identical Microsoft VCSamples RGS fixtures.",
        "// Commit: 9e1d4475555b76a17a3568369867f1d7b6cc6126 (MIT)",
        "#pragma once",
        "#include <string_view>",
        "namespace OpenATLRgsSamples {",
        "struct Sample { const wchar_t* name; std::wstring_view content; };",
    ]
    samples = []
    for name, digest in EXPECTED_GIT_BLOBS.items():
        data = (directory / name).read_bytes()
        git_blob = hashlib.sha1(
            b"blob " + str(len(data)).encode("ascii") + b"\0" + data
        ).hexdigest()
        if git_blob != digest:
            raise SystemExit(
                f"RGS provenance mismatch: {name}: {git_blob} != {digest}"
            )
        text = data.decode("ascii")
        if ')OpenATLRGS"' in text:
            raise SystemExit(f"RGS raw literal delimiter collision: {name}")
        identifier = name.replace(".rgs", "")
        lines.append(
            f'inline constexpr std::wstring_view {identifier} = '
            f'LR"OpenATLRGS({text})OpenATLRGS";'
        )
        samples.append(f'{{L"{name}", {identifier}}}')
    lines.append("inline constexpr Sample All[] = {" + ",".join(samples) + "};")
    lines.append("} // namespace OpenATLRgsSamples")
    # Always emit LF regardless of the Windows host's newline translation.
    output.write_bytes(("\n".join(lines) + "\n").encode("utf-8"))

if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("embed_fixtures.py OUTPUT DIRECTORY")
    make(Path(sys.argv[1]), Path(sys.argv[2]))
