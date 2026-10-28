#!/usr/bin/env python3
"""Provide Windows resource path spellings for llvm-rc on POSIX hosts.

llvm-rc does not unescape or translate backslashes in asset filenames. Copy
the original binary under those literal names, leaving upstream input intact.
"""
from pathlib import Path
import shutil
import sys


def main() -> None:
    source, relative, destination = sys.argv[1:]
    root = Path(destination)
    root.mkdir(parents=True, exist_ok=True)
    for separator in ("\\", "\\\\"):
        shutil.copyfile(source, root / relative.replace("/", separator))


if __name__ == "__main__":
    main()
