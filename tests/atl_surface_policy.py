#!/usr/bin/env python3
"""Reject server, registration and false-success ATL APIs from OpenATL source.

This is a source-surface gate, separate from the real native WTL10 GUI
and opt-in COM Client build/runtime gates. The supported Win32 modules
remain part of ATL; COM class factories and registry writers do not.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
HEADERS = sorted((ROOT / "include").glob("*.h"))
FORBIDDEN_FILES = ("atlcom.h",)
FORBIDDEN_TOKENS = (
    r"\bCRegKey\b",
    r"\bCComModule\b",
    r"\bCAtlModuleT\b",
    r"\bCAtlComModule\b",
    r"\bCAtlModule\b",
    r"\bCComObjectRootBase\b",
    r"\bCComObjectRootEx\b",
    r"\bCComObjectCached\b",
    r"\bCComObject\b",
    r"\bCComClassFactory\b",
    r"\bCComCreator2?\b",
    r"\bIObjectWithSiteImpl\b",
    r"\bAtlAxWinInit\b",
    r"\bAtlInternalQueryInterface\b",
    r"\bOBJECT_ENTRY_AUTO\b",
    r"\bBEGIN_COM_MAP\b",
    r"\bCOM_INTERFACE_ENTRY\w*\b",
    r"\b_ATL_OBJMAP_ENTRY\w*\b",
    r"\b_ATL_INTMAP_ENTRY\b",
    r"\b_ATL_COM_MODULE\w*\b",
    r"\b_ATL_REGMAP_ENTRY\b",
    r"\b_AtlComModule\b",
    r"\b_pAtlModule\b",
    r"\b_pModule\b",
    r"\bRegisterServer\s*\(",
    r"\bUnregisterServer\s*\(",
    r"\bAddCommonRGSReplacements\s*\(",
    r"\bRegCreateKeyEx(?:A|W)?\b",
    r"\bRegSetValueEx(?:A|W)?\b",
    r"\bRegDeleteKey(?:A|W)?\b",
    r"\bRegDeleteValue(?:A|W)?\b",
    r"\bRegEnumKeyEx(?:A|W)?\b",
)
BAD = re.compile("|".join(FORBIDDEN_TOKENS))
EXPECTED = {
    "atlbase.h": "class CAtlWinModule",
    "atlcore.h": "class CAtlBaseModule",
    "atlcomcli.h": "class CComPtr",
    "atlwin.h": "class CWindowImplBaseT",
}


def main() -> int:
    failures = []
    for forbidden in FORBIDDEN_FILES:
        if (ROOT / "include" / forbidden).exists():
            failures.append(f"include/{forbidden}: COM Server header must be deleted")
    for path in HEADERS:
        data = path.read_text(encoding="utf-8")
        for match in BAD.finditer(data):
            line = data.count("\n", 0, match.start()) + 1
            failures.append(f"{path.relative_to(ROOT)}:{line}: {match.group(0)}")
    for name, marker in EXPECTED.items():
        path = ROOT / "include" / name
        if not path.exists() or marker not in path.read_text(encoding="utf-8"):
            failures.append(f"include/{name}: missing required Windows UI/COM Client contract")
    if failures:
        for item in failures:
            print("OPENATL_UI_CLIENT_SURFACE_FAIL " + item, file=sys.stderr)
        return 1
    print(f"OPENATL_UI_CLIENT_SURFACE_OK headers={len(HEADERS)} "
          "com_server=0 registry_writers=0 fake_activation=0 "
          "win32_ui=1 com_client=1")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
