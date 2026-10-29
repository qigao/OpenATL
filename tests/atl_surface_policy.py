#!/usr/bin/env python3
"""Check the implemented ATL Client, Server and WTL source surface.

OpenATL targets full ATL source compatibility. Standard APIs not yet
implemented (DLL modules, COM object maps, RGS, type libraries) must NOT
be blacklisted just because they were previously out of scope. Native
tests independently verify actual UI/COM behavior and ownership.
"""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
REQUIRED = {
    "atlbase.h": ("class CAtlWinModule", '#include "atlreg.h"'),
    "atlcore.h": ("class CAtlBaseModule",),
    "atlwin.h": ("class ATL_NO_VTABLE CWindowImplBaseT",),
    "atlcomcli.h": ("class CComPtr", "class CComVariant", "class CComBSTR"),
    "atlcom.h": ("class CComObjectRootEx", "class CComObject :",
                 "class CComCoClass", "#define BEGIN_COM_MAP",
                 "#define OBJECT_ENTRY_AUTO", "#define DECLARE_NO_REGISTRY",
                 "#define DECLARE_REGISTRY_RESOURCEID"),
    "openatl/atl_module.h": ("class CAtlModule", "class CAtlExeModuleT",
                             "class ATL_NO_VTABLE CAtlDllModuleT",
                             "struct AutoObjectEntry", "autoObjectEntries",
                             "AutoRegistryMethod", "ApplyAutoRegistry",
                             "CoRegisterClassObject", "CoRevokeClassObject"),
    "openatl/com_server.h": ("class Module final", "class ModuleReference final",
                             "class ClassFactory final"),
    "openatl/com_registration.h": ("ClassRegistration", "RegisterClass",
                                   "UnregisterClass"),
    "openatl/rgs_parser.h": ("enum class Directive", "MaxNesting",
                            "inline Error Parse", "inline Error ResolveModule"),
    "openatl/rgs_registration.h": ("RegisterRgs(", "UnregisterRgs(",
                                   "PreflightNew", "registration_detail::remove",
                                   "OpenATL.COMOwner", "NoRemove"),
    "openatl/rgs_resource.h": ("ScopedRgsRegistrationContext",
                                "FindResourceW", "ReadRgsResource(",
                                "UpdateRegistryResource(", "MB_ERR_INVALID_CHARS"),
    "atlreg.h": ("class CRegKey", "RegGetValue", "RegCreateKeyEx"),
}


def main() -> int:
    failures = []
    for name, markers in REQUIRED.items():
        path = ROOT / "include" / name
        if not path.is_file():
            failures.append(f"Missing implemented ATL Client/Server header: {name}")
            continue
        source = path.read_text(encoding="utf-8")
        for marker in markers:
            if marker not in source:
                failures.append(f"{name}: missing implemented API marker: {marker}")

    if failures:
        for failure in failures:
            print("OPENATL_ATL_COMPAT_SURFACE_FAIL " + failure, file=sys.stderr)
        return 1
    print("OPENATL_ATL_COMPAT_SURFACE_OK "
          "ui=1 com_client=1 com_server=1 auto_object_map=1 registration=1 gaps=tracked")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
