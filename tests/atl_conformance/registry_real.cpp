// P1-C: real-but-SANDBOX-ONLY CAtlModuleT::RegisterServer writes/rollback.
// Uses pinned test-patched OpenATL and a new disposable HKCU key per process.
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <objbase.h>
#include <objidl.h>
#include <ocidl.h>
#include <cstdio>
#include <cwchar>
#include <cstdlib>
#include <cstring>
#include <string>
#include <atlbase.h>
#include <atlcom.h>
static_assert(sizeof(void*)==8,"native Windows x64/ARM64 COM registrar test");
#ifdef __RATL__
#error "Never mix RATL and OpenATL headers"
#endif
const CLSID P1B_CLASS_A={
    0x1ab0965f,0x76cd,0x48ae,{0xb9,0xa8,0x44,0x21,0x96,0x0a,0x35,0x82}};
extern "C" const CLSID P1B_CLASS_B;
static const CLSID UNKNOWN_CLASS={
    0x720ec938,0xc085,0x48b2,{0xa1,0xac,0x9a,0x8e,0xc7,0x33,0x69,0x7d}};
class TestModule : public ATL::CAtlModuleT<TestModule> {};
static TestModule module;
class AutoClassA : public ATL::CComObjectRootEx<ATL::CComSingleThreadModel>,
                   public IPersist {
public:
    BEGIN_COM_MAP(AutoClassA)
        COM_INTERFACE_ENTRY_IID(IID_IPersist,IPersist)
    END_COM_MAP()
    DECLARE_NOT_AGGREGATABLE(AutoClassA)
    STDMETHOD(GetClassID)(CLSID* id) override {
        if(!id)return E_POINTER;
        *id=P1B_CLASS_A;return S_OK;
    }
};
OBJECT_ENTRY_AUTO(P1B_CLASS_A,AutoClassA)

static bool classKey(REFCLSID clsid,wchar_t* path,unsigned length) {
    wchar_t guid[40]={};
    if(!::StringFromGUID2(clsid,guid,40))return false;
    unsigned n=0;const wchar_t* prefix=L"CLSID\\";
    for(unsigned i=0;prefix[i];++i)path[n++]=prefix[i];
    for(unsigned i=0;guid[i];++i) {
        if(n+1>=length)return false;
        path[n++]=guid[i];
    }
    path[n]=0;return true;
}
static bool contains(HKEY root,REFCLSID clsid) {
    wchar_t path[64]={};
    if(!classKey(clsid,path,64))return false;
    HKEY h=nullptr;
    const LSTATUS hr=::RegOpenKeyExW(root,path,0,KEY_READ,&h);
    if(h)::RegCloseKey(h);
    return hr==ERROR_SUCCESS;
}
static bool hasValue(HKEY root,REFCLSID clsid) {
    wchar_t path[84]={};
    if(!classKey(clsid,path,84))return false;
    unsigned i=::lstrlenW(path);
    const wchar_t* end=L"\\LocalServer32";
    for(unsigned j=0;end[j];++j)path[i++]=end[j];
    path[i]=0;
    HKEY h=nullptr;
    if(::RegOpenKeyExW(root,path,0,KEY_READ,&h)!=ERROR_SUCCESS)
        return false;
    wchar_t value[80]={};
    DWORD type=0,size=sizeof(value);
    const LSTATUS err=::RegQueryValueExW(h,nullptr,nullptr,&type,
        reinterpret_cast<BYTE*>(value),&size);
    ::RegCloseKey(h);
    return err==ERROR_SUCCESS && type==REG_SZ &&
        ::lstrcmpW(value,L"TEST-ONLY-NOT-AN-EXECUTABLE")==0 &&
        size==static_cast<DWORD>((::lstrlenW(value)+1)*sizeof(wchar_t));
}
static bool absent(HKEY root,REFCLSID id){return !contains(root,id);}
struct Scope {
    std::wstring path;
    HKEY root=nullptr;
    DWORD proof=0;
    bool owned=false,redirected=false,armed=false;
    bool create() {
        GUID id={};
        if(FAILED(::CoCreateGuid(&id)))return false;
        wchar_t text[40]={};
        if(!::StringFromGUID2(id,text,40))return false;
        path=L"qigao_tcc_ATL_P1C_";
        path+=text;
        proof=id.Data1 ^ 0xA1339C47u;
        if(proof==0)proof=0x13777BC5u;
        DWORD disp=0;
        LSTATUS hr=::RegCreateKeyExW(HKEY_CURRENT_USER,path.c_str(),
            0,nullptr,REG_OPTION_NON_VOLATILE,KEY_ALL_ACCESS,nullptr,
            &root,&disp);
        if(hr!=ERROR_SUCCESS || disp!=REG_CREATED_NEW_KEY) {
            if(root){::RegCloseKey(root);root=nullptr;}
            path.clear(); // never delete a collision
            return false;
        }
        owned=true;
        return ::RegSetValueExW(root,L"P1C_REDIRECT_PROOF",0,REG_DWORD,
            reinterpret_cast<const BYTE*>(&proof),sizeof(proof))==ERROR_SUCCESS;
    }
    bool authorize() {
        if(!owned || ::RegOverridePredefKey(HKEY_CLASSES_ROOT,root)!=ERROR_SUCCESS)
            return false;
        redirected=true;
        if(ATL::AtlSandboxRegistryArm(root,proof)!=S_OK)
            return false;
        armed=true;return true;
    }
    bool cleanup() {
        if(armed) {
            if(ATL::AtlSandboxRegistryDisarm()!=S_OK)return false;
            armed=false;
        }
        if(redirected) {
            if(::RegOverridePredefKey(HKEY_CLASSES_ROOT,nullptr)!=ERROR_SUCCESS)
                return false;
            redirected=false;
        }
        if(root){::RegCloseKey(root);root=nullptr;}
        if(!owned)return true;
        if(::RegDeleteTreeW(HKEY_CURRENT_USER,path.c_str())!=ERROR_SUCCESS)
            return false;
        HKEY stale=nullptr;
        LSTATUS hr=::RegOpenKeyExW(HKEY_CURRENT_USER,path.c_str(),0,KEY_READ,&stale);
        if(stale)::RegCloseKey(stale);
        if(hr!=ERROR_FILE_NOT_FOUND || stale)return false;
        owned=false;path.clear();return true;
    }
    ~Scope() {
        if(owned || root || redirected || armed)
            if(!cleanup())std::fputs("REGISTRAR_CLEANUP_FAILED\n",stderr);
    }
};
static int failed(int code,const char* part) {
    std::fprintf(stderr,"ATL_P1C_REAL_FAIL code=%d step=%s\n",code,part);
    return code;
}
static DWORD WINAPI foreignRegistration(void* out) {
    *static_cast<HRESULT*>(out)=module.RegisterServer();
    return 0;
}
// The only cross-process authority is a newly created GUID-scoped HKCU
// root, with a matching marker derived from its GUID. Only parent-created
// child processes receive its path/proof through inherited environment.
static bool recoveryRequest(std::wstring& path,DWORD& proof) {
    wchar_t raw[104]={},hex[24]={};
    DWORD p=::GetEnvironmentVariableW(L"ATL_P1C_RECOVERY_KEY",raw,104);
    DWORD q=::GetEnvironmentVariableW(L"ATL_P1C_RECOVERY_PROOF",hex,24);
    if(!p || p>=104 || !q || q>=24)return false;
    const std::wstring prefix=L"qigao_tcc_ATL_P1C_";
    const std::wstring key(raw);
    if(key.length()!=prefix.size()+38 || key.compare(0,prefix.size(),prefix)!=0)
        return false;
    GUID id={};
    if(FAILED(::CLSIDFromString(raw+prefix.size(),&id)))return false;
    wchar_t canonical[40]={};
    if(!::StringFromGUID2(id,canonical,40))return false;
    if(key.substr(prefix.size())!=canonical)return false;
    wchar_t* end=nullptr;
    unsigned long parsed=std::wcstoul(hex,&end,16);
    if(end==hex || *end!=0 || parsed==0 || parsed>0xFFFFFFFFul)
        return false;
    DWORD expectedProof=id.Data1 ^ 0xA1339C47u;
    if(expectedProof==0)expectedProof=0x13777BC5u;
    if(static_cast<DWORD>(parsed)!=expectedProof)
        return false;
    path=key;
    proof=static_cast<DWORD>(parsed);
    return true;
}
static bool proofFor(HKEY key,DWORD expected) {
    DWORD actual=0,type=0,bytes=sizeof(actual);
    const LSTATUS hr=::RegQueryValueExW(key,L"P1C_REDIRECT_PROOF",
          nullptr,&type,reinterpret_cast<BYTE*>(&actual),&bytes);
    return hr==ERROR_SUCCESS && type==REG_DWORD &&
           bytes==sizeof(actual) && actual==expected;
}
static int recoveryChild(const char* mode) {
    std::wstring path;
    DWORD proof=0;
    if(!recoveryRequest(path,proof))return failed(60,"bad_recovery_authority");
    HKEY key=nullptr;
    if(::RegOpenKeyExW(HKEY_CURRENT_USER,path.c_str(),0,KEY_ALL_ACCESS,
       &key)!=ERROR_SUCCESS)return failed(61,"recovery_scratch_missing");
    if(!proofFor(key,proof)) {
        ::RegCloseKey(key);
        return failed(62,"recovery_marker_incorrect");
    }
    if(std::strcmp(mode,"--abandon")==0) {
        if(::RegOverridePredefKey(HKEY_CLASSES_ROOT,key)!=ERROR_SUCCESS)
            return failed(63,"child_override");
        if(ATL::AtlSandboxRegistryArm(key,proof)!=S_OK)
            return failed(64,"child_scope_authorize");
        if(module.RegisterServer()!=S_OK ||
           !hasValue(key,P1B_CLASS_A) || !hasValue(key,P1B_CLASS_B))
            return failed(65,"child_real_registration");
        if(::RegFlushKey(key)!=ERROR_SUCCESS)
            return failed(66,"child_persist_to_hive");
        // Intentionally bypass C++ destructors and RegOverridePredefKey
        // reset. This changes no HKCR state in other processes.
        ::TerminateProcess(::GetCurrentProcess(),77);
        return failed(67,"unexpected_termination_return");
    }
    if(std::strcmp(mode,"--recover")==0) {
        const bool persisted=hasValue(key,P1B_CLASS_A) &&
                             hasValue(key,P1B_CLASS_B);
        ::RegCloseKey(key);
        if(!persisted)return failed(68,"orphaned_entries_missing");
        // Separate fresh process with *no* HKCR override, clean only
        // the GUID-scoped HKCU root after validating ownership proof.
        if(::RegDeleteTreeW(HKEY_CURRENT_USER,path.c_str())!=ERROR_SUCCESS)
            return failed(69,"orphaned_key_cleanup");
        HKEY stale=nullptr;
        const LSTATUS status=::RegOpenKeyExW(HKEY_CURRENT_USER,
                          path.c_str(),0,KEY_READ,&stale);
        if(stale)::RegCloseKey(stale);
        return status==ERROR_FILE_NOT_FOUND && !stale
             ? 0 : failed(70,"stale_key_after_recovery");
    }
    ::RegCloseKey(key);
    return failed(71,"unknown_recovery_mode");
}
static bool spawnSelf(const wchar_t* mode,DWORD expected) {
    wchar_t path[MAX_PATH]={};
    DWORD n=::GetModuleFileNameW(nullptr,path,MAX_PATH);
    if(!n || n>=MAX_PATH)return false;
    std::wstring command=L"\"";
    command+=path;
    command+=L"\" ";
    command+=mode;
    STARTUPINFOW startup={};
    startup.cb=sizeof(startup);
    PROCESS_INFORMATION process={};
    if(!::CreateProcessW(path,&command[0],nullptr,nullptr,FALSE,
         CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))
        return false;
    const DWORD waited=::WaitForSingleObject(process.hProcess,15000);
    if(waited!=WAIT_OBJECT_0) {
        ::TerminateProcess(process.hProcess,99);
        ::WaitForSingleObject(process.hProcess,5000);
    }
    DWORD code=0;
    const BOOL read=::GetExitCodeProcess(process.hProcess,&code);
    ::CloseHandle(process.hThread);
    ::CloseHandle(process.hProcess);
    return waited==WAIT_OBJECT_0 && read && code==expected;
}
struct RecoveryEnvGuard {
    ~RecoveryEnvGuard() {
        ::SetEnvironmentVariableW(L"ATL_P1C_RECOVERY_KEY",nullptr);
        ::SetEnvironmentVariableW(L"ATL_P1C_RECOVERY_PROOF",nullptr);
    }
};
static int crashRecovery() {
    Scope scope;
    if(!scope.create())return failed(80,"create_recovery_scratch");
    wchar_t hex[24]={};
    std::swprintf(hex,24,L"%08lX",
                  static_cast<unsigned long>(scope.proof));
    RecoveryEnvGuard env;
    if(!::SetEnvironmentVariableW(L"ATL_P1C_RECOVERY_KEY",scope.path.c_str()) ||
       !::SetEnvironmentVariableW(L"ATL_P1C_RECOVERY_PROOF",hex))
        return failed(81,"pass_scoped_recovery_authority");
    if(!spawnSelf(L"--abandon",77) ||
       !hasValue(scope.root,P1B_CLASS_A) ||
       !hasValue(scope.root,P1B_CLASS_B))
        return failed(82,"abrupt_exit_or_unflushed_registry");
    // Closing the parent handle allows the recovery child to perform
    // complete removal with no handles held by the parent.
    ::RegCloseKey(scope.root);
    scope.root=nullptr;
    if(!spawnSelf(L"--recover",0))
        return failed(83,"independent_recovery_cleanup");
    HKEY remaining=nullptr;
    const LSTATUS status=::RegOpenKeyExW(HKEY_CURRENT_USER,
                  scope.path.c_str(),0,KEY_READ,&remaining);
    if(remaining)::RegCloseKey(remaining);
    if(status!=ERROR_FILE_NOT_FOUND || remaining)
        return failed(84,"residual_key_after_second_process");
    scope.owned=false;
    scope.path.clear();
    return 0;
}
int main(int argc,char** argv) {
    if(argc==2)return recoveryChild(argv[1]);
    if(argc!=1)return failed(90,"unexpected_arguments");
    HRESULT apartment=::CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(apartment))return failed(1,"apartment");
    Scope scope;
    int error=0;
    if(module.RegisterServer()!=E_NOTIMPL)error=10;
    if(!error && !scope.create())error=11;
    if(!error && ATL::AtlSandboxRegistryArm(scope.root,scope.proof)!=E_ACCESSDENIED)
        error=12;
    if(!error && !scope.authorize())error=13;
    if(!error && (module.RegisterServer(TRUE)!=E_NOTIMPL ||
                   module.UnregisterServer(TRUE)!=E_NOTIMPL))error=14;
    if(!error && module.RegisterServer(FALSE,&UNKNOWN_CLASS)!=CLASS_E_CLASSNOTAVAILABLE)
        error=15;
    if(!error) {
        HRESULT foreign=S_OK;
        HANDLE worker=::CreateThread(nullptr,0,foreignRegistration,
                                     &foreign,0,nullptr);
        if(!worker)error=16;
        else {
            const DWORD wait=::WaitForSingleObject(worker,5000);
            ::CloseHandle(worker);
            if(wait!=WAIT_OBJECT_0 || foreign!=RPC_E_WRONG_THREAD)
                error=17;
        }
    }
    if(!error && (ATL::AtlSandboxRegistryFailAt(1)!=S_OK ||
        module.RegisterServer()!=E_OUTOFMEMORY ||
        !absent(scope.root,P1B_CLASS_A) || !absent(scope.root,P1B_CLASS_B)))
        error=18;
    if(!error && (ATL::AtlSandboxRegistryFailAt(-1)!=S_OK ||
        module.RegisterServer()!=S_OK ||
        !hasValue(scope.root,P1B_CLASS_A) ||
        !hasValue(scope.root,P1B_CLASS_B)))error=19;
    if(!error && (module.RegisterServer()!=HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS) ||
        !hasValue(scope.root,P1B_CLASS_A) ||
        !hasValue(scope.root,P1B_CLASS_B)))error=20;
    if(!error && (module.UnregisterServer(FALSE)!=S_OK ||
        !absent(scope.root,P1B_CLASS_A) ||
        !absent(scope.root,P1B_CLASS_B) ||
        module.UnregisterServer(FALSE)!=S_OK))error=21;
    if(!error && (module.RegisterServer(FALSE,&P1B_CLASS_A)!=S_OK ||
        !hasValue(scope.root,P1B_CLASS_A) || !absent(scope.root,P1B_CLASS_B) ||
        module.UnregisterServer(FALSE,&P1B_CLASS_A)!=S_OK ||
        !absent(scope.root,P1B_CLASS_A)))error=22;
    if(!error && ATL::AtlSandboxRegistryDisarm()!=S_OK)error=23;
    else if(!error)scope.armed=false;
    if(!error && module.RegisterServer()!=E_NOTIMPL)error=24;
    const bool clean=scope.cleanup();
    ::CoUninitialize();
    if(error)return failed(error,"registrar_contract");
    if(!clean)return failed(25,"sandbox_cleanup");
    const int recovered=crashRecovery();
    if(recovered)return recovered;
    std::puts("ATL_P1C_REAL_OK guarded=1 writes=2 rollback=1 duplicate=1 targeted=1 clean=1 recovered=1");
    return 0;
}
