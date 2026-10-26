#!/usr/bin/env python3
"""Test-only OpenATL P1-B auto object-map qualification on an exact pinned checkout.

This neither imports RATL implementation nor enables persistent registry writes.
The patch fails closed on upstream drift and modifies only temporary CI checkouts.
"""
from pathlib import Path
import subprocess
import sys

PIN = "7b587f3e10f3f8d93a01522033de80007368bad5"

BASE_ANCHOR = "typedef _ATL_OBJMAP_ENTRY30 _ATL_OBJMAP_ENTRY;\n"
BASE_INSERT = r"""
// P1-B experiment: bounded automatic object map shared by inline functions
// across translation units. POC only; module objects require apartment owner.
inline _ATL_OBJMAP_ENTRY** AtlAutoObjectSlots() noexcept
{
    static _ATL_OBJMAP_ENTRY* slots[16] = {};
    return slots;
}
inline unsigned& AtlAutoObjectCount() noexcept
{
    static unsigned count = 0;
    return count;
}
inline HRESULT& AtlAutoObjectError() noexcept
{
    static HRESULT error = S_OK;
    return error;
}
inline bool AtlRegisterAutoObjectEntry(_ATL_OBJMAP_ENTRY* entry) noexcept
{
    if (entry == NULL || entry->pclsid == NULL || entry->pCF == NULL ||
        entry->pfnCreateInstance == NULL || entry->pfnGetClassObject == NULL) {
        AtlAutoObjectError() = E_INVALIDARG;
        return false;
    }
    unsigned& count = AtlAutoObjectCount();
    if (count >= 16) {
        AtlAutoObjectError() = HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        return false;
    }
    for (unsigned i = 0; i < count; ++i) {
        if (::IsEqualCLSID(*AtlAutoObjectSlots()[i]->pclsid, *entry->pclsid)) {
            AtlAutoObjectError() = HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
            return false;
        }
    }
    AtlAutoObjectSlots()[count++] = entry;
    return true;
}

inline HRESULT WINAPI AtlAutoGetClassObject(
    void* factory, REFIID iid, LPVOID* out) noexcept
{
    if (out == NULL)
        return E_POINTER;
    *out = NULL;
    if (factory == NULL)
        return CLASS_E_CLASSNOTAVAILABLE;
    return static_cast<IUnknown*>(factory)->QueryInterface(iid, out);
}
"""

MODULE_OLD = r"""class CAtlComModule : public _ATL_COM_MODULE {
public:
    CAtlComModule() noexcept
    {
        cbSize = sizeof(_ATL_COM_MODULE);
        m_hInstTypeLib = NULL;
        m_csObjMap.Init();
    }

    ~CAtlComModule()
    {
        m_csObjMap.Term();
    }
};"""
MODULE_NEW = r"""class CAtlComModule : public _ATL_COM_MODULE {
public:
    CAtlComModule() noexcept
    {
        cbSize = sizeof(_ATL_COM_MODULE);
        m_hInstTypeLib = NULL;
        m_csObjMap.Init();
    }

    ~CAtlComModule()
    {
        // No persistent registry writes; revoke active class objects.
        RevokeClassObjects();
        m_csObjMap.Term();
    }

    HRESULT GetClassObject(REFCLSID clsid, REFIID iid, LPVOID* out) noexcept
    {
        if (out == NULL)
            return E_POINTER;
        *out = NULL;
        if (FAILED(AtlAutoObjectError()))
            return AtlAutoObjectError();
        for (unsigned i = 0; i < AtlAutoObjectCount(); ++i) {
            _ATL_OBJMAP_ENTRY* e = AtlAutoObjectSlots()[i];
            if (::IsEqualCLSID(clsid, *e->pclsid))
                return e->pfnGetClassObject(e->pCF, iid, out);
        }
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    HRESULT RegisterClassObjects(DWORD context, DWORD flags,
                                 int injectFailureIndex = -1) noexcept
    {
        // Qualification path is single-apartment. Never register on a
        // different thread from a previous still-active registration.
        if (m_registrationThread != 0 &&
            m_registrationThread != ::GetCurrentThreadId())
            return RPC_E_WRONG_THREAD;
        if (FAILED(AtlAutoObjectError()))
            return AtlAutoObjectError();
        const unsigned count = AtlAutoObjectCount();
        if (count == 0)
            return CLASS_E_CLASSNOTAVAILABLE;
        for (unsigned i = 0; i < count; ++i) {
            if (AtlAutoObjectSlots()[i]->dwRegister != 0)
                return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
        }
        HRESULT hr = S_OK;
        for (unsigned i = 0; i < count; ++i) {
            if ((int)i == injectFailureIndex) {
                hr = E_OUTOFMEMORY;
                break;
            }
            _ATL_OBJMAP_ENTRY* e = AtlAutoObjectSlots()[i];
            DWORD cookie = 0;
            hr = ::CoRegisterClassObject(*e->pclsid, e->pCF,
                                         context, flags, &cookie);
            if (FAILED(hr))
                break;
            e->dwRegister = cookie;
            m_registrationThread = ::GetCurrentThreadId();
        }
        if (FAILED(hr)) {
            // A mid-batch failure is never treated as partial success.
            const HRESULT rollback = RevokeClassObjects();
            if (FAILED(rollback))
                return rollback;
        }
        return hr;
    }

    HRESULT RevokeClassObjects() noexcept
    {
        if (m_registrationThread != 0 &&
            m_registrationThread != ::GetCurrentThreadId())
            return RPC_E_WRONG_THREAD;
        HRESULT result = S_FALSE;
        for (unsigned i = 0; i < AtlAutoObjectCount(); ++i) {
            _ATL_OBJMAP_ENTRY* e = AtlAutoObjectSlots()[i];
            if (e->dwRegister == 0)
                continue;
            HRESULT hr = ::CoRevokeClassObject(e->dwRegister);
            if (FAILED(hr))
                return hr; // keep cookie for explicit retry
            e->dwRegister = 0;
            result = S_OK;
        }
        m_registrationThread = 0;
        return result;
    }

private:
    DWORD m_registrationThread = 0;
};"""

MACRO_OLD = r"""#define OBJECT_ENTRY_AUTO(clsid, x) \
    __declspec(selectany) ATL::_ATL_OBJMAP_ENTRY __objMap_##x = \
    { &clsid, NULL, x::_CreatorClass::CreateInstance, x::_CreatorClass::CreateInstance, NULL, 0, NULL };"""
MACRO_NEW = r"""// Explicit P1-B test-only auto-map: bounded registration and static factories.
// Keep factory lifetime until process exit; no borrowed cross-DSO ownership.
#define OBJECT_ENTRY_AUTO(clsid, x) \
    static ATL::CComClassFactory __atlFactory_##x; \
    static ATL::_ATL_OBJMAP_ENTRY __objMap_##x = \
    { &(clsid), NULL, ATL::AtlAutoGetClassObject, \
      x::_CreatorClass::CreateInstance, \
      static_cast<IUnknown*>(&__atlFactory_##x), 0, NULL }; \
    static bool __atlRegistered_##x = []() noexcept { \
        __atlFactory_##x.SetVoid( \
            reinterpret_cast<void*>(&x::_CreatorClass::CreateInstance)); \
        return ATL::AtlRegisterAutoObjectEntry(&__objMap_##x); \
    }();"""


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("Usage: openatl-com-p1b-map.py PINNED_CHECKOUT")
    root = Path(sys.argv[1]).resolve(strict=True)
    sha = subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    if sha != PIN:
        raise SystemExit(f"Refuse non-pinned OpenATL checkout: {sha}")
    replacements = {
        "atlbase.h": [(BASE_ANCHOR, BASE_ANCHOR + BASE_INSERT),
                      (MODULE_OLD, MODULE_NEW)],
        "atlcom.h": [(MACRO_OLD, MACRO_NEW)],
    }
    pending = {}
    for filename, items in replacements.items():
        path = root / "include" / filename
        contents = path.read_text(encoding="utf-8")
        for original, replacement in items:
            if contents.count(original) != 1:
                raise SystemExit("Pinned P1-B map anchor mismatch in "
                                 + filename + ": " + original[:70])
            contents = contents.replace(original, replacement)
        pending[path] = contents
    for path, contents in pending.items():
        path.write_text(contents, encoding="utf-8")
    print("Pinned OpenATL test-only P1-B automatic object map patched")


if __name__ == "__main__":
    main()
