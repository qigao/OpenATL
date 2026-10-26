#!/usr/bin/env python3
"""Test-only, sandbox-authorized real registry writes for pinned OpenATL.

Patch *temporary* OpenATL checkout after P1 and P1-B patches. It never
copies RATL implementation, writes through HKEY_CLASSES_ROOT, enables
global COM registration, or modifies a production checkout.
"""
from pathlib import Path
import subprocess
import sys

PIN = "7b587f3e10f3f8d93a01522033de80007368bad5"
ANCHOR = "template <typename T>\nclass CAtlModuleT : public CAtlModule {\npublic:\n"
OLD = r"""    // Not implemented: reporting S_OK would silently lie about registry state.
    HRESULT RegisterServer(BOOL /*bRegTypeLib*/ = FALSE, const CLSID* /*pCLSID*/ = NULL) noexcept
    {
        return E_NOTIMPL;
    }

    HRESULT UnregisterServer(BOOL /*bUnRegTypeLib*/, const CLSID* /*pCLSID*/ = NULL) noexcept
    {
        return E_NOTIMPL;
    }"""
NEW = r"""    // Remains unsupported unless THIS PROCESS explicitly armed a proven,
    // GUID-scoped registry sandbox. No writes to real HKEY_CLASSES_ROOT.
    HRESULT RegisterServer(BOOL bRegTypeLib = FALSE, const CLSID* clsid = NULL) noexcept
    {
        if (bRegTypeLib)
            return E_NOTIMPL; // type libraries require a separate gate
        return AtlSandboxRegisterServer(true, clsid);
    }

    HRESULT UnregisterServer(BOOL bUnRegTypeLib, const CLSID* clsid = NULL) noexcept
    {
        if (bUnRegTypeLib)
            return E_NOTIMPL;
        return AtlSandboxRegisterServer(false, clsid);
    }"""
HELPER = r"""// Test-only *bounded* registry registrar. COM RGS/DllRegisterServer is
// intentionally not implemented. The handle MUST come from a disposable
// HKCU subkey independently verified through process-local HKCR remapping.
// Every actual write goes to this explicit handle, NEVER to HKCR or HKLM.
struct AtlSandboxRegistryState {
    HKEY root = NULL;
    DWORD threadId = 0;
    DWORD proof = 0;
    int injectFailureIndex = -1;
};
inline AtlSandboxRegistryState& AtlSandboxRegistry() noexcept
{
    static AtlSandboxRegistryState state;
    return state;
}
inline bool AtlSandboxProofMatches(HKEY root, DWORD proof) noexcept
{
    if (root == NULL || proof == 0)
        return false;
    DWORD own=0, visible=0, type1=0, type2=0;
    DWORD n1=sizeof(own), n2=sizeof(visible);
    const bool direct = (::RegQueryValueExW(
        root, L"P1C_REDIRECT_PROOF", NULL, &type1,
        reinterpret_cast<BYTE*>(&own), &n1) == ERROR_SUCCESS);
    if (!direct || type1 != REG_DWORD || n1 != sizeof(own) || own != proof)
        return false;
    const bool remapped = (::RegQueryValueExW(
        HKEY_CLASSES_ROOT, L"P1C_REDIRECT_PROOF", NULL, &type2,
        reinterpret_cast<BYTE*>(&visible), &n2) == ERROR_SUCCESS);
    return remapped && type2 == REG_DWORD && n2 == sizeof(visible) &&
           visible == proof;
}
inline HRESULT AtlSandboxRegistryArm(HKEY root, DWORD proof) noexcept
{
    AtlSandboxRegistryState& state=AtlSandboxRegistry();
    if (state.root != NULL)
        return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    if (!AtlSandboxProofMatches(root, proof))
        return E_ACCESSDENIED;
    state.root=root;
    state.proof=proof;
    state.threadId=::GetCurrentThreadId();
    state.injectFailureIndex=-1;
    return S_OK;
}
inline HRESULT AtlSandboxRegistryFailAt(int index) noexcept
{
    AtlSandboxRegistryState& state=AtlSandboxRegistry();
    if (state.root == NULL)
        return E_NOTIMPL;
    if (state.threadId != ::GetCurrentThreadId())
        return RPC_E_WRONG_THREAD;
    state.injectFailureIndex=index;
    return S_OK;
}
inline HRESULT AtlSandboxRegistryDisarm() noexcept
{
    AtlSandboxRegistryState& state=AtlSandboxRegistry();
    if (state.root == NULL)
        return S_FALSE;
    if (state.threadId != ::GetCurrentThreadId())
        return RPC_E_WRONG_THREAD;
    state=AtlSandboxRegistryState{};
    return S_OK;
}
inline bool AtlSandboxClassKey(REFCLSID clsid, wchar_t* path,
                              unsigned length) noexcept
{
    wchar_t guid[40]={};
    if (::StringFromGUID2(clsid, guid, 40) == 0 || length < 50)
        return false;
    const wchar_t* prefix=L"CLSID\\";
    unsigned n=0;
    for (; prefix[n] != 0; ++n)
        path[n]=prefix[n];
    for (unsigned j=0; guid[j]!=0 && n+1<length; ++j)
        path[n++]=guid[j];
    path[n]=0;
    return n < length-1;
}
inline HRESULT AtlSandboxRegistryClassWrite(HKEY root, REFCLSID clsid) noexcept
{
    wchar_t path[64]={};
    if (!AtlSandboxClassKey(clsid, path, 64))
        return E_INVALIDARG;

    // Do not overwrite existing classes, even in the authorized sandbox.
    HKEY existing=NULL;
    const LSTATUS seen=::RegOpenKeyExW(root, path, 0, KEY_READ, &existing);
    if (existing != NULL)
        ::RegCloseKey(existing);
    if (seen == ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    if (seen != ERROR_FILE_NOT_FOUND)
        return HRESULT_FROM_WIN32(seen);

    wchar_t localServer[84]={};
    unsigned i=0;
    for (; path[i]!=0; ++i)
        localServer[i]=path[i];
    const wchar_t* suffix=L"\\LocalServer32";
    for (unsigned j=0;suffix[j]!=0;++j)
        localServer[i++]=suffix[j];
    localServer[i]=0;
    HKEY entry=NULL;
    DWORD disposition=0;
    LSTATUS status=::RegCreateKeyExW(
        root,localServer,0,NULL,REG_OPTION_NON_VOLATILE,
        KEY_ALL_ACCESS,NULL,&entry,&disposition);
    if (status != ERROR_SUCCESS) {
        // Parent might have been created before the failure.
        ::RegDeleteTreeW(root,path);
        return HRESULT_FROM_WIN32(status);
    }
    if (disposition != REG_CREATED_NEW_KEY) {
        ::RegCloseKey(entry);
        return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    }
    const wchar_t* value=L"TEST-ONLY-NOT-AN-EXECUTABLE";
    status=::RegSetValueExW(entry,NULL,0,REG_SZ,
        reinterpret_cast<const BYTE*>(value),
        static_cast<DWORD>((::lstrlenW(value)+1)*sizeof(wchar_t)));
    ::RegCloseKey(entry);
    if (status != ERROR_SUCCESS) {
        const LSTATUS removed=::RegDeleteTreeW(root,path);
        return HRESULT_FROM_WIN32(
            removed == ERROR_SUCCESS ? status : removed);
    }
    return S_OK;
}
inline HRESULT AtlSandboxRegistryClassDelete(HKEY root, REFCLSID clsid) noexcept
{
    wchar_t path[64]={};
    if (!AtlSandboxClassKey(clsid,path,64))
        return E_INVALIDARG;
    const LSTATUS status=::RegDeleteTreeW(root,path);
    if (status == ERROR_FILE_NOT_FOUND)
        return S_OK; // idempotent unregister
    return HRESULT_FROM_WIN32(status);
}
inline HRESULT AtlSandboxRegisterServer(bool enable, const CLSID* selected) noexcept
{
    AtlSandboxRegistryState& state=AtlSandboxRegistry();
    if (state.root == NULL)
        return E_NOTIMPL;
    if (state.threadId != ::GetCurrentThreadId())
        return RPC_E_WRONG_THREAD;
    if (!AtlSandboxProofMatches(state.root,state.proof))
        return E_ACCESSDENIED;
    if (FAILED(AtlAutoObjectError()))
        return AtlAutoObjectError();
    const unsigned count=AtlAutoObjectCount();
    if (count==0 || count>16)
        return CLASS_E_CLASSNOTAVAILABLE;

    bool found=false;
    bool installed[16]={};
    unsigned attempted=0;
    HRESULT result=S_OK;
    for (unsigned i=0; i<count; ++i) {
        _ATL_OBJMAP_ENTRY* entry=AtlAutoObjectSlots()[i];
        if (selected!=NULL && !::IsEqualCLSID(*selected,*entry->pclsid))
            continue;
        found=true;
        if (enable && static_cast<int>(attempted)==state.injectFailureIndex) {
            result=E_OUTOFMEMORY;
            break;
        }
        ++attempted;
        result=enable
          ? AtlSandboxRegistryClassWrite(state.root,*entry->pclsid)
          : AtlSandboxRegistryClassDelete(state.root,*entry->pclsid);
        if (FAILED(result))
            break;
        installed[i]=enable;
    }
    if (!found)
        return CLASS_E_CLASSNOTAVAILABLE;
    if (enable && FAILED(result)) {
        // Never roll back an entry that existed before this call.
        for (unsigned i=0; i<count; ++i) {
            if (!installed[i])
                continue;
            const HRESULT hr=AtlSandboxRegistryClassDelete(
                state.root,*AtlAutoObjectSlots()[i]->pclsid);
            if (FAILED(hr))
                return hr; // do NOT declare partial rollback successful
        }
    }
    return result;
}
"""

def main() -> None:
    if len(sys.argv)!=2:
        raise SystemExit("Usage: openatl-com-p1c-registrar.py PINNED_CHECKOUT")
    root=Path(sys.argv[1]).resolve(strict=True)
    sha=subprocess.check_output(
        ["git","-C",str(root),"rev-parse","HEAD"],text=True).strip()
    if sha!=PIN:
        raise SystemExit("Refusing unknown OpenATL checkout: "+sha)
    header=root/"include"/"atlbase.h"
    source=header.read_text(encoding="utf-8")
    for old,new in ((ANCHOR,HELPER+"\n"+ANCHOR),(OLD,NEW)):
        if source.count(old)!=1:
            raise SystemExit("Fail-closed P1-C anchor not found or ambiguous: "+old[:65])
        source=source.replace(old,new)
    if "AtlSandboxRegistryArm" not in source or "return E_NOTIMPL" not in source:
        raise SystemExit("Missing authorization/unsupported guards")
    header.write_text(source,encoding="utf-8")
    print("Pinned OpenATL P1-C test-only registrar: authorized HKCU writes only")

if __name__=="__main__":
    main()
