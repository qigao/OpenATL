// P1-C: prove disposable HKCR registry redirection before enabling any
// real ATL persistent registrar. This test NEVER calls a true registrar.
// Written only into a newly created, GUID-scoped HKCU scratch subtree.
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <objbase.h>
#include <string>
#include <cstdio>
#include <atlbase.h>
static_assert(sizeof(void*)==8,"Windows x64/ARM64 registry fixture required");

class TestModule : public ATL::CAtlModuleT<TestModule> {};
static TestModule module;

struct ScratchRegistry {
    std::wstring path;
    HKEY key=nullptr;
    bool overridden=false;

    bool begin() {
        GUID guid={};
        if(FAILED(::CoCreateGuid(&guid))) return false;
        wchar_t text[48]={};
        if(!::StringFromGUID2(guid,text,48)) return false;
        path=L"qigao_tcc_ATL_P1C_";
        path+=text;
        DWORD disposition=0;
        const LSTATUS create=::RegCreateKeyExW(
            HKEY_CURRENT_USER,path.c_str(),0,nullptr,REG_OPTION_NON_VOLATILE,
            KEY_ALL_ACCESS,nullptr,&key,&disposition);
        if(create!=ERROR_SUCCESS || disposition!=REG_CREATED_NEW_KEY) {
            // A collision must never make cleanup delete an existing user's
            // key, however unlikely a UUID collision may be.
            if(key) { ::RegCloseKey(key); key=nullptr; }
            path.clear();
            return false;
        }
        // This changes ONLY the calling process's interpretation of HKCR.
        if(::RegOverridePredefKey(HKEY_CLASSES_ROOT,key)!=ERROR_SUCCESS)
            return false;
        overridden=true;
        return true;
    }

    bool prove_readonly_redirect() {
        // Crucial: write the proof ONLY through the known sandbox handle.
        // Verify it can be READ via remapped HKCR *before* any HKCR write.
        const DWORD sentinel=0x1A7C2026u;
        if(::RegSetValueExW(key,L"P1C_REDIRECT_PROOF",0,REG_DWORD,
                           reinterpret_cast<const BYTE*>(&sentinel),
                           sizeof(sentinel))!=ERROR_SUCCESS)
            return false;
        DWORD value=0,kind=0,bytes=sizeof(value);
        if(::RegQueryValueExW(HKEY_CLASSES_ROOT,L"P1C_REDIRECT_PROOF",
            nullptr,&kind,reinterpret_cast<BYTE*>(&value),&bytes)!=ERROR_SUCCESS)
            return false;
        return kind==REG_DWORD && bytes==sizeof(value) && value==sentinel;
    }

    bool verify_rollback() {
        // This is NOT DllRegisterServer: exercise only redirected Win32 APIs.
        // Scope is a randomized CLSID-like path inside the temporary root.
        const wchar_t* sub=L"CLSID\\{3EA76115-8ED8-4D5B-8F77-D4ACF3614A21}";
        const wchar_t* entry=L"CLSID\\{3EA76115-8ED8-4D5B-8F77-D4ACF3614A21}\\LocalServer32";
        HKEY out=nullptr;
        DWORD disposition=0;
        if(::RegCreateKeyExW(HKEY_CLASSES_ROOT,entry,0,nullptr,
             REG_OPTION_NON_VOLATILE,KEY_ALL_ACCESS,nullptr,&out,
             &disposition)!=ERROR_SUCCESS)
            return false;
        const wchar_t* value=L"TEST-ONLY-NOT-AN-EXECUTABLE";
        const LSTATUS set=::RegSetValueExW(
            out,nullptr,0,REG_SZ,reinterpret_cast<const BYTE*>(value),
            (DWORD)((wcslen(value)+1)*sizeof(wchar_t)));
        ::RegCloseKey(out);
        if(set!=ERROR_SUCCESS)
            return false;

        // Verify the write reached the temporary HKCU subtree, not HKCR.
        HKEY verify=nullptr;
        if(::RegOpenKeyExW(key,
             L"CLSID\\{3EA76115-8ED8-4D5B-8F77-D4ACF3614A21}\\LocalServer32",
             0,KEY_READ,&verify)!=ERROR_SUCCESS)
            return false;
        wchar_t observed[80]={};
        DWORD type=0,len=sizeof(observed);
        const LSTATUS get=::RegQueryValueExW(verify,nullptr,nullptr,
             &type,reinterpret_cast<BYTE*>(observed),&len);
        ::RegCloseKey(verify);
        if(get!=ERROR_SUCCESS || type!=REG_SZ || wcscmp(observed,value)!=0)
            return false;

        // Simulate an aborted registration by explicitly removing its entire
        // temporary class tree and asserting disappearance.
        if(::RegDeleteTreeW(HKEY_CLASSES_ROOT,sub)!=ERROR_SUCCESS)
            return false;
        HKEY stale=nullptr;
        const LSTATUS missing=::RegOpenKeyExW(key,sub,0,KEY_READ,&stale);
        if(stale) ::RegCloseKey(stale);
        return missing==ERROR_FILE_NOT_FOUND && stale==nullptr;
    }

    bool close() {
        if(overridden) {
            // Always restore process HKCR before touching the scratch root.
            if(::RegOverridePredefKey(HKEY_CLASSES_ROOT,nullptr)!=ERROR_SUCCESS)
                return false;
            overridden=false;
        }
        if(key) {
            ::RegCloseKey(key);
            key=nullptr;
        }
        if(path.empty()) return true;
        if(::RegDeleteTreeW(HKEY_CURRENT_USER,path.c_str())!=ERROR_SUCCESS)
            return false;
        HKEY stale=nullptr;
        const LSTATUS check=::RegOpenKeyExW(HKEY_CURRENT_USER,
            path.c_str(),0,KEY_READ,&stale);
        if(stale) ::RegCloseKey(stale);
        if(check!=ERROR_FILE_NOT_FOUND || stale!=nullptr) return false;
        path.clear();
        return true;
    }

    ~ScratchRegistry() {
        if(overridden || key || !path.empty()) {
            if(!close())
                std::fprintf(stderr,"P1C_REGISTRY_CLEANUP_FAILED\n");
        }
    }
};

int main() {
    ScratchRegistry scope;
    // Never write through HKCR without a proven successful override.
    int failure=0;
    if(!scope.begin()) failure=10;
    if(!failure && !scope.prove_readonly_redirect()) failure=11;
    if(!failure &&
       (module.RegisterServer()!=E_NOTIMPL ||
        module.UnregisterServer(FALSE)!=E_NOTIMPL))
        failure=12;
    if(!failure && !scope.verify_rollback()) failure=13;
    const bool cleaned=scope.close();
    if(!cleaned && !failure) failure=14;
    if(failure) {
        std::fprintf(stderr,"ATL_REG_SANDBOX_FAIL code=%d\n",failure);
        return failure;
    }
    std::puts("ATL_REG_SANDBOX_OK isolated=1 redirect=1 rollback=1 restored=1 clean=1 stub_failclosed=1");
    return 0;
}
