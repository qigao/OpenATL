#!/usr/bin/env python3
"""Inspect the exact installed WTL10 public registry surface, not an adapter.

The audited bytes come exclusively from qigao/vcpkg-cache's pinned WTL port.
This is a source/API inventory gate, not yet a behavior/parity proof.
"""
from __future__ import annotations
from pathlib import Path
import os
import re
import sys

def main() -> int:
    root = os.environ.get("OPENATL_WTL_VCPKG_INSTALLED_DIR", "")
    triplet = os.environ.get("OPENATL_WTL_VCPKG_TRIPLET", "")
    if not root or not re.fullmatch(r"[A-Za-z0-9_.+-]+", triplet):
        raise ValueError("require installed vcpkg WTL identity")
    prefix = Path(root) / triplet
    stamp = (prefix / "share/wtl/openatl-wtl-source.txt").read_text("utf-8")
    expected = ("wtl=10.0.10320#6\nsha512="
                "086a6cf6a49a4318a8c519136ba6019ded7aa7f2c1d85f78c30b21183654537"
                "b3428a400a64fcdacba3a7a10a9ef05137b6f2119f59594da300d55f9ebfb1309\n"
                "provider=vcpkg-cache/ports/wtl\n")
    if stamp.replace("\r\n", "\n") != expected:
        raise ValueError("source revision mismatch")
    misc = (prefix / "include/atlmisc.h").read_text("latin-1")
    ctrlx = (prefix / "include/atlctrlx.h").read_text("latin-1")
    begin = "// CRegProperty and CRegPropertyImpl<> - properties stored in registry"
    end = "// Global functions for stock GDI objects"
    x, y = misc.find(begin), misc.find(end)
    if x < 0 or y <= x or y - x > 180_000:
        raise ValueError("the full original CRegProperty section is missing/changed")
    section = misc[x:y].splitlines()
    if not re.search(r"\bclass\s+CRegProperty\b", misc[x:y]):
        raise ValueError("original CRegProperty public declaration is absent")
    if "CHyperLinkImpl" not in ctrlx or "class CHyperLink" not in ctrlx:
        raise ValueError("original CHyperLink class removed")
    if "ReadFromRegistry(" not in misc or "WriteToRegistry(" not in misc:
        raise ValueError("original MRU registry operations removed")

    # Record original interface lines to guide further behavioral tests.
    start_line = misc[:x].count("\n") + 1
    pattern = re.compile(
        r"^\s*(?:template\s*<|class\s+|struct\s+|public:|protected:|"
        r"BOOL\s+|bool\s+|HRESULT\s+|LSTATUS\s+|LONG\s+|DWORD\s+|void\s+|"
        r"int\s+|CRegProperty(?:Impl)?\s*\(|const\s+|static\s+)")
    signatures = [(start_line + i, line.strip()) for i, line in enumerate(section)
                  if pattern.match(line)]
    print(f"WTL_REGISTRY_API_INVENTORY_OK provider=vcpkg original=1 "
          f"registry_section_lines={len(section)} "
          f"signatures={len(signatures)} hyperlink=1 mru=1")
    for line_no, line in signatures[:100]:
        print(f"WTL_REGISTRY_API {line_no}: {line[:190]}")
    # Original WTL10 uses macro-based property maps. Keep their declared
    # spelling in the audit before adding CRTP serialization admission.
    macros = sorted(set(re.findall(r"(?m)^\s*#\s*define\s+([A-Z][A-Z0-9_]+)", misc[x:y])))
    print("WTL_REGISTRY_MAP_MACROS " + ",".join(macros))
    if len(signatures) > 100:
        print(f"WTL_REGISTRY_API remaining={len(signatures) - 100}")

    # Pinned WTL10 ReadOne treats missing optional settings as successful,
    # preserving defaults. Typed ReadProp still carries Win32 LSTATUS.
    if "return (lRet == ERROR_SUCCESS) || (lRet == ERROR_FILE_NOT_FOUND);" not in misc[x:y]:
        raise ValueError("pinned WTL ReadOne optional-missing contract changed")
    print("WTL_REGISTRY_MISSING_CONTRACT_OK optional_missing=1 typed_error=preserved")
    return 0

if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as e:
        print("WTL_REGISTRY_API_FAIL " + str(e), file=sys.stderr)
        raise SystemExit(1)
