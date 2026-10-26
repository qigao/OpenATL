#!/usr/bin/env python3
"""Strict test-only OpenATL COM P1 patch. Never modifies upstream or installed headers.

Applies *only* to pinned temporary test checkouts. No source from RATL is
copied or linked. No COM registration success is simulated.
"""
from pathlib import Path
import subprocess
import sys

PIN = "7b587f3e10f3f8d93a01522033de80007368bad5"

PATCHES = {
    "atlbase.h": [
        (r"""    HRESULT RegisterServer(BOOL /*bRegTypeLib*/ = FALSE, const CLSID* /*pCLSID*/ = NULL) noexcept
    {
        return S_OK;
    }

    HRESULT UnregisterServer(BOOL /*bUnRegTypeLib*/, const CLSID* /*pCLSID*/ = NULL) noexcept
    {
        return S_OK;
    }""",
         r"""    // Not implemented: reporting S_OK would silently lie about registry state.
    HRESULT RegisterServer(BOOL /*bRegTypeLib*/ = FALSE, const CLSID* /*pCLSID*/ = NULL) noexcept
    {
        return E_NOTIMPL;
    }

    HRESULT UnregisterServer(BOOL /*bUnRegTypeLib*/, const CLSID* /*pCLSID*/ = NULL) noexcept
    {
        return E_NOTIMPL;
    }"""),
    ],
    "atlcom.h": [
        (r"""template <typename T1, typename T2>
class CComCreator2 {""",
         r"""// Reject aggregation rather than constructing a non-aggregatable object.
template <HRESULT hrFailure>
class CComFailCreator {
public:
    static HRESULT WINAPI CreateInstance(void*, REFIID, LPVOID* ppv) noexcept
    {
        if (ppv == NULL)
            return E_POINTER;
        *ppv = NULL;
        return hrFailure;
    }
};

template <typename T1, typename T2>
class CComCreator2 {"""),
        (r"""        ATLASSERT(m_pfnCreateInstance != NULL);
        if (ppvObj == NULL)
            return E_POINTER;
        *ppvObj = NULL;""",
         r"""        if (ppvObj == NULL)
            return E_POINTER;
        *ppvObj = NULL;
        if (m_pfnCreateInstance == NULL)
            return CLASS_E_CLASSNOTAVAILABLE;"""),
        (r"""#define DECLARE_NOT_AGGREGATABLE(x) \
public: \
    typedef ATL::CComCreator< ATL::CComObject<x> > _CreatorClass;""",
         r"""#define DECLARE_NOT_AGGREGATABLE(x) \
public: \
    typedef ATL::CComCreator2< ATL::CComCreator<ATL::CComObject<x> >, \
        ATL::CComFailCreator<CLASS_E_NOAGGREGATION> > _CreatorClass;"""),
    ],
}


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("Usage: openatl-com-p1-hardening.py OPENATL_TEMP_CHECKOUT")
    checkout = Path(sys.argv[1]).resolve(strict=True)
    sha = subprocess.check_output(
        ["git", "-C", str(checkout), "rev-parse", "HEAD"], text=True).strip()
    if sha != PIN:
        raise SystemExit("Refusing modified/unpinned OpenATL checkout: " + sha)
    # Validate every anchor before writing any file.
    updated = {}
    for file, replacements in PATCHES.items():
        path = checkout / "include" / file
        original = path.read_text(encoding="utf-8")
        text = original
        for before, after in replacements:
            if text.count(before) != 1:
                raise SystemExit(f"Unexpected existing source in {file}: {before[:75]!r}")
            text = text.replace(before, after)
        updated[path] = text
    for file, content in updated.items():
        file.write_text(content, encoding="utf-8")
    print("Pinned OpenATL COM P1 test-only patch: fail-closed registry/creator/factory")


if __name__ == "__main__":
    main()
