#!/usr/bin/env python3
"""Reject obsolete modules/registrars and keep optional COM APIs isolated.

This is a source-surface gate, separate from the real native WTL10 GUI
and opt-in COM Client build/runtime gates. Ordinary registry key operations
are isolated to atlreg.h; explicit COM registration lives under openatl/.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
HEADERS = sorted((ROOT / "include").rglob("*.h"))
FORBIDDEN_FILES = ("statreg.h",)
FORBIDDEN_TOKENS = (
    r"\bCRegObject\b",
    r"\bIRegistrarBase\b",
    r"\bCComModule\b",
    r"\bCAtlModuleT\b",
    r"\bCAtlComModule\b",
    r"\bCAtlModule\b",
    r"\bCComObjectCached\b",
    r"\bCComClassFactory\b",
    r"\bIObjectWithSiteImpl\b",
    r"\bAtlAxWinInit\b",
    r"\bAtlInternalQueryInterface\b",
    r"\bOBJECT_ENTRY_AUTO\b",
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
)
REGISTRY_WRITE_TOKENS = (
    r"\bRegCreateKeyEx(?:A|W)?\b",
    r"\bRegSetValueEx(?:A|W)?\b",
    r"\bRegDeleteKey(?:A|W)?\b",
    r"\bRegDeleteValue(?:A|W)?\b",
    r"\bRegEnumKeyEx(?:A|W)?\b",
)
BAD = re.compile("|".join(FORBIDDEN_TOKENS))
BAD_REGISTRY = re.compile("|".join(REGISTRY_WRITE_TOKENS))
EXPECTED = {
    "atlbase.h": ("class CAtlWinModule", "struct DLGTEMPLATEEX", '#include "atlreg.h"'),
    "atlcore.h": ("class CAtlBaseModule",),
    "atlcom.h": ("class CComObjectRootEx", "class CComObject :", "class CComCoClass", "#define BEGIN_COM_MAP"),
    "atlcomcli.h": ("class CComPtr",),
    "atlwin.h": ("class ATL_NO_VTABLE CWindowImplBaseT",),
    "atldef.h": ("#define _ATL_NO_HOSTING 1",),
    "atlreg.h": ("class CRegKey", "RegGetValue", "RegOpenKeyEx", "RegCreateKeyEx"),
    "openatl/com_server.h": ("class Module final", "class ModuleReference final", "class ClassFactory final"),
    "openatl/com_registration.h": ("ClassRegistration", "RegisterClass", "UnregisterClass"),
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
        relative = path.relative_to(ROOT / "include").as_posix()
        if relative not in ("atlreg.h", "openatl/com_registration.h"):
            for match in BAD_REGISTRY.finditer(data):
                line = data.count("\n", 0, match.start()) + 1
                failures.append(f"{path.relative_to(ROOT)}:{line}: registry API outside its owning header: {match.group(0)}")
        if "/" not in relative and relative != "atlcom.h" and re.search(r'#\s*include\s*[<"]openatl/com_', data):
            failures.append(f"{relative}: UI/client headers must not include optional COM server APIs")
    for name, markers in EXPECTED.items():
        path = ROOT / "include" / name
        if not path.exists():
            failures.append(f"include/{name}: missing required Windows UI/COM Client contract")
            continue
        source = path.read_text(encoding="utf-8")
        for marker in markers:
            if marker not in source:
                failures.append(
                    f"include/{name}: missing required Windows UI/COM Client contract: {marker}"
                )
    if failures:
        for item in failures:
            print("OPENATL_UI_CLIENT_SURFACE_FAIL " + item, file=sys.stderr)
        return 1
    print(f"OPENATL_UI_CLIENT_SURFACE_OK headers={len(HEADERS)} "
          "com_server=explicit registry_owner=isolated fake_activation=0 "
          "win32_ui=1 com_client=1")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
