#!/usr/bin/env python3
"""Inspect the exact installed WTL10 public registry surface, not an adapter.

The audited bytes come exclusively from our vendored and hashed WTL source.
This is a source/API inventory gate, not yet a behavior/parity proof.
"""
from __future__ import annotations
from pathlib import Path
import re
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "wtl10"))
from manifest_contract import verify_repository_wtl_stamp

def main() -> int:
    # Audit the same byte-integrity checked source that production compiles.
    verify_repository_wtl_stamp()
    source = Path(__file__).resolve().parents[2] / "wtl" / "Include"
    misc = (source / "atlmisc.h").read_text("latin-1")
    ctrlx = (source / "atlctrlx.h").read_text("latin-1")
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
    print(f"WTL_REGISTRY_API_INVENTORY_OK provider=repo original=1 "
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
    # The inventory below is from the repository-owned release, never an external
    # mirrored header. Missing caps/CRTP/callback contracts fail closed.
    for item in (
        "struct BinaryProp", "struct CharArrayProp",
        "if(uSize <= binProp.uSize)", "if(uSize <= caProp.uSize)",
        "LSTATUS ReadProp(LPCTSTR lpstrRegValue, HFONT& hFont)",
        "LSTATUS WriteProp(LPCTSTR lpstrRegValue, HFONT& hFont)",
        "class CRegPropertyImpl : public CRegProperty",
        "#define BEGIN_REGPROP_MAP", "#define REG_PROPERTY",
        "#define END_REGPROP_MAP()", "OnReadError(", "OnWriteError(",
    ):
        if item not in misc[x:y]:
            raise ValueError("original WTL property ABI changed: " + item)
    if misc[x:y].count("lRet = ERROR_OUTOFMEMORY;") < 2:
        raise ValueError("original WTL bounded-buffer rejection changed")
    print("WTL_REGISTRY_EXTENDED_CONTRACT_OK binary=1 char_array=1 font=1 map=1 callbacks=1 bounds=1")
    # Original hyperlink code must still read both documented HKCU values
    # through the single OpenATL::CRegKey Win32 API; no static color fallback.
    for token in ('"Anchor Color"', '"Anchor Color Visited"',
                  "QueryStringValue(", "_ParseColorString(",
                  "Software\\\\Microsoft\\\\Internet Explorer\\\\Settings"):
        if token not in ctrlx:
            raise ValueError("original WTL hyperlink registry source changed: " + token)
    print("WTL_HYPERLINK_REGISTRY_SOURCE_OK anchor=1 visited=1 parser=1")
    # Exact-release API details to qualify next: binary/font/character arrays
    # and CRTP property-map macros. Bounded source dump from the repository source,
    # never a different WTL tag or an edited/adapted copy.
    for label, start_token, end_token, max_lines in (
        ("BINARY", "struct BinaryProp", "struct CharArrayProp", 24),
        ("CHAR_ARRAY", "struct CharArrayProp", "CRegProperty()", 24),
        ("FONT_READ", "LSTATUS ReadProp(LPCTSTR lpstrRegValue, HFONT& hFont)", "LSTATUS WriteProp(LPCTSTR lpstrRegValue, HFONT& hFont)", 42),
        ("FONT_WRITE", "LSTATUS WriteProp(LPCTSTR lpstrRegValue, HFONT& hFont)", "LSTATUS ReadProp(LPCTSTR lpstrRegValue, BinaryProp& binProp)", 32),
        ("BINARY_READ", "LSTATUS ReadProp(LPCTSTR lpstrRegValue, BinaryProp& binProp)", "LSTATUS WriteProp(LPCTSTR lpstrRegValue, BinaryProp& binProp)", 30),
        ("BINARY_WRITE", "LSTATUS WriteProp(LPCTSTR lpstrRegValue, BinaryProp& binProp)", "LSTATUS ReadProp(LPCTSTR lpstrRegValue, CharArrayProp& caProp)", 25),
        ("CHAR_READ", "LSTATUS ReadProp(LPCTSTR lpstrRegValue, CharArrayProp& caProp)", "LSTATUS WriteProp(LPCTSTR lpstrRegValue, CharArrayProp& caProp)", 30),
        ("CHAR_WRITE", "LSTATUS WriteProp(LPCTSTR lpstrRegValue, CharArrayProp& caProp)", "LSTATUS ReadProp(LPCTSTR lpstrRegValue, ATL::CString& strProp)", 22),
        ("MAP", "#define BEGIN_REGPROP_MAP", "class CRegPropertyImpl", 20),
        ("IMPL", "class CRegPropertyImpl", "void OnWriteError", 75),
    ):
        begin_idx = next((i for i, line in enumerate(section) if start_token in line), -1)
        end_idx = next((i for i in range(max(begin_idx + 1, 0), len(section))
                        if end_token in section[i]), -1)
        if begin_idx < 0 or end_idx < begin_idx:
            raise ValueError("pinned original WTL property source boundary missing: " + label)
        for offset, content in enumerate(section[begin_idx:min(end_idx, begin_idx + max_lines)], begin_idx):
            print(f"WTL_REGISTRY_SOURCE_{label} {start_line + offset}: {content[:180]}")
    return 0

if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as e:
        print("WTL_REGISTRY_API_FAIL " + str(e), file=sys.stderr)
        raise SystemExit(1)
