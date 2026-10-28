"""Explicit source-charset reader for Clang's UTF-8-only C++ frontend.

Only the selected file is decoded; no C++ tokens or archived files are edited.
Compilation/dependency generation still belong to the selected Clang driver.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--charset", required=True)
    parser.add_argument("compiler", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.compiler
    if command and command[0] == "--":
        command = command[1:]
    if not command:
        parser.error("Compiler command is required")
    source = args.source.resolve(strict=True)
    selected = os.path.normcase(str(source))
    index = next((i for i, value in enumerate(command[1:], 1)
                  if os.path.normcase(os.path.abspath(value)) == selected), None)
    if index is None:
        return subprocess.call(command)
    if source.stat().st_size > 4 * 1024 * 1024:
        raise ValueError("Legacy source exceeds the 4 MiB input limit")
    text = source.read_bytes().decode(args.charset, errors="strict")
    data = ("#line 1 " + json.dumps(source.as_posix(), ensure_ascii=False) + "\n" + text).encode("utf-8")
    command[index] = "-"
    command[1:1] = ["-x", "c++", "-iquote", str(source.parent), "-finput-charset=UTF-8"]
    return subprocess.run(command, input=data).returncode

if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, UnicodeError, ValueError) as error:
        print(f"COMPILER_INPUT_ERROR: {error}", file=sys.stderr)
        sys.exit(2)
