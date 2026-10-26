// Independent RATL ATL RGS parser reference on Windows x64.
// These are process-local HKCR-overridden, disposable HKCU writes.
// NEVER accept user-supplied scripts; RATL can recognize HKCU/HKLM roots.
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <objbase.h>
#include <cstdio>
#include <cwchar>
#include <string>
#include <atlbase.h>
#include <statreg.h>
#ifndef __RATL__
#error "This fixture must use ONLY pinned LGPL-licensed RATL headers"
#endif
static_assert(sizeof(void*)==8,"RATL registry reference Windows x64 only");

static const wchar_t* script =
  L"HKCR\n{\n"
  L" RATL_REG_REF\n {\n"
  L"  val Text = s 'RATL_TEST_ONLY'\n"
  L"  val Number = d '42'\n"
  L" }\n"
  L"}\n";
static const wchar_t* badScript =
  L"HKCR\n{\n"
  L" RATL_REG_REF_BAD\n {\n"
  L"  val Broken = z 'cannot-register'\n"
  L" }\n"
  L"}\n";

struct Sandbox {
    std::wstring path;
    HKEY key=nullptr;
    bool owned=false;
    bool mapped=false;
    bool begin() {
        GUID g={};
        if(FAILED(::CoCreateGuid(&g)))return false;
        wchar_t text[40]={};
        if(!::StringFromGUID2(g,text,40))return false;
        path=L"qigao_tcc_RATL_REG_";
        path+=text;
        DWORD state=0;
        if(::RegCreateKeyExW(HKEY_CURRENT_USER,path.c_str(),0,nullptr,
          REG_OPTION_NON_VOLATILE,KEY_ALL_ACCESS,nullptr,&key,&state)!=ERROR_SUCCESS ||
          state!=REG_CREATED_NEW_KEY) {
            if(key){::RegCloseKey(key);key=nullptr;}
            path.clear(); // collision must not delete another user's key
            return false;
        }
        owned=true;
        const DWORD marker=g.Data1 ? g.Data1 : 1;
        if(::RegSetValueExW(key,L"RATL_REF_MARKER",0,REG_DWORD,
             reinterpret_cast<const BYTE*>(&marker),sizeof(marker))!=ERROR_SUCCESS)
            return false;
        if(::RegOverridePredefKey(HKEY_CLASSES_ROOT,key)!=ERROR_SUCCESS)
            return false;
        mapped=true;
        DWORD val=0,kind=0,bytes=sizeof(val);
        return ::RegQueryValueExW(HKEY_CLASSES_ROOT,L"RATL_REF_MARKER",
          nullptr,&kind,reinterpret_cast<BYTE*>(&val),&bytes)==ERROR_SUCCESS &&
          kind==REG_DWORD && bytes==sizeof(val) && val==marker;
    }
    bool cleanup() {
        if(mapped) {
            if(::RegOverridePredefKey(HKEY_CLASSES_ROOT,nullptr)!=ERROR_SUCCESS)
                return false;
            mapped=false;
        }
        if(key){::RegCloseKey(key);key=nullptr;}
        if(!owned)return true;
        if(::RegDeleteTreeW(HKEY_CURRENT_USER,path.c_str())!=ERROR_SUCCESS)
            return false;
        HKEY stale=nullptr;
        const LSTATUS status=::RegOpenKeyExW(HKEY_CURRENT_USER,path.c_str(),
                                          0,KEY_READ,&stale);
        if(stale)::RegCloseKey(stale);
        if(status!=ERROR_FILE_NOT_FOUND || stale)return false;
        owned=false;
        path.clear();
        return true;
    }
    ~Sandbox() {
        if(owned || key || mapped)
            if(!cleanup())std::fputs("RATL_SCRATCH_CLEANUP_FAILED\n",stderr);
    }
};
static bool absent(HKEY root,const wchar_t* child) {
    HKEY h=nullptr;
    const LSTATUS status=::RegOpenKeyExW(root,child,0,KEY_READ,&h);
    if(h)::RegCloseKey(h);
    return status==ERROR_FILE_NOT_FOUND && h==nullptr;
}
static bool values(HKEY root) {
    HKEY child=nullptr;
    if(::RegOpenKeyExW(root,L"RATL_REG_REF",0,KEY_READ,&child)!=ERROR_SUCCESS)
        return false;
    wchar_t text[64]={};
    DWORD t=0,bytes=sizeof(text);
    const LSTATUS str=::RegQueryValueExW(child,L"Text",nullptr,&t,
             reinterpret_cast<BYTE*>(text),&bytes);
    DWORD num=0,numType=0,numBytes=sizeof(num);
    const LSTATUS integer=::RegQueryValueExW(child,L"Number",nullptr,
             &numType,reinterpret_cast<BYTE*>(&num),&numBytes);
    ::RegCloseKey(child);
    return str==ERROR_SUCCESS && t==REG_SZ &&
           ::lstrcmpW(text,L"RATL_TEST_ONLY")==0 &&
           integer==ERROR_SUCCESS && numType==REG_DWORD &&
           numBytes==sizeof(num) && num==42;
}
static int failed(int code,const char* stage) {
    std::fprintf(stderr,"RATL_REG_REF_FAIL code=%d stage=%s\n",code,stage);
    return code;
}
int main() {
    Sandbox scratch;
    if(!scratch.begin())return failed(10,"sandbox_proof");
    ATL::CRegObject reg;
    if(reg.StringRegister(script)!=S_OK || !values(scratch.key))
        return failed(11,"rgs_register_values");
    if(reg.StringUnregister(script)!=S_OK ||
       !absent(scratch.key,L"RATL_REG_REF"))
        return failed(12,"rgs_unregister_absence");
    const HRESULT reject=reg.StringRegister(badScript);
    if(SUCCEEDED(reject) || !absent(scratch.key,L"RATL_REG_REF_BAD"))
        return failed(13,"invalid_rgs_rollback");
    if(!scratch.cleanup())return failed(14,"sandbox_cleanup");
    std::puts("RATL_REG_REF_OK rgs=1 string=1 dword=1 unregister=1 invalid=1 clean=1");
    return 0;
}
