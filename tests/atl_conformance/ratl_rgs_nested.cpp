/* P1-D: pinned RATL statreg.h differential regression, without copying
 * RATL implementation. Every RGS input below is a literal HKCR-only string.
 * Run unsafe recursive-delete paths in independent bounded Windows processes.
 * MIT fixture code; LGPL/GPL RATL remains an isolated external checkout.
 */
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <objbase.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <string>
#include <atlbase.h>
#include <statreg.h>
#ifndef __RATL__
#error "RATL-only reference; never mix OpenATL headers"
#endif
static_assert(sizeof(void*)==8,"Native Windows x64 RATL reference only");

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

static const wchar_t * const kNested =
    L"HKCR\n{\n RATL_P1D_TREE\n {\n  Child\n  {\n"
    L"   val Text = s 'nested'\n  }\n }\n}\n";
static const wchar_t * const kForce =
    L"HKCR\n{\n ForceRemove RATL_P1D_FORCE\n"
    L" {\n  val New = s 'fresh'\n }\n}\n";
static const wchar_t * const kDelete =
    L"HKCR\n{\n Delete RATL_P1D_DELETE\n}\n";
static const wchar_t * const kDeniedForce =
    L"HKCR\n{\n ForceRemove RATL_P1D_DENIED\n"
    L" {\n  val New = s 'should-not-write'\n }\n}\n";
static const wchar_t * const kDeniedDelete =
    L"HKCR\n{\n Delete RATL_P1D_DENIED\n}\n";
static const wchar_t * const kNoRemove =
    L"HKCR\n{\n NoRemove RATL_P1D_KEEP\n"
    L" {\n  val Marker = s 'retained'\n }\n}\n";
static const wchar_t * const kTypes =
    L"HKCR\n{\n RATL_P1D_TYPES\n {\n"
    L"  val String = s 'plain'\n"
    L"  val Expand = e 'not-expanded'\n"
    L"  val Number = d '0x2A'\n"
    L"  val Bytes = b '0102A0FF'\n"
    L" }\n}\n";

static bool exists(HKEY root,const wchar_t* name) {
    HKEY key=nullptr;
    const LSTATUS err=::RegOpenKeyExW(root,name,0,KEY_READ,&key);
    if(key)::RegCloseKey(key);
    return err==ERROR_SUCCESS;
}
static bool seed(HKEY root,const wchar_t* path) {
    HKEY key=nullptr;
    DWORD disposition=0;
    const LSTATUS status=::RegCreateKeyExW(
        root,path,0,nullptr,REG_OPTION_NON_VOLATILE,
        KEY_ALL_ACCESS,nullptr,&key,&disposition);
    if(key)::RegCloseKey(key);
    return status==ERROR_SUCCESS && disposition==REG_CREATED_NEW_KEY;
}
static bool str(HKEY root,const wchar_t* keyName,const wchar_t* valueName,
                DWORD expectedType,const wchar_t* expected) {
    HKEY key=nullptr;
    if(::RegOpenKeyExW(root,keyName,0,KEY_READ,&key)!=ERROR_SUCCESS)
        return false;
    wchar_t data[80]={};
    DWORD type=0,n=sizeof(data);
    const LSTATUS err=::RegQueryValueExW(key,valueName,nullptr,&type,
        reinterpret_cast<BYTE*>(data),&n);
    ::RegCloseKey(key);
    return err==ERROR_SUCCESS && type==expectedType &&
        n==static_cast<DWORD>((::lstrlenW(expected)+1)*sizeof(wchar_t)) &&
        ::lstrcmpW(data,expected)==0;
}
static bool dwordValue(HKEY root) {
    HKEY key=nullptr;
    if(::RegOpenKeyExW(root,L"RATL_P1D_TYPES",0,KEY_READ,&key)!=ERROR_SUCCESS)
        return false;
    DWORD value=0,type=0,n=sizeof(value);
    const LSTATUS err=::RegQueryValueExW(key,L"Number",nullptr,&type,
        reinterpret_cast<BYTE*>(&value),&n);
    ::RegCloseKey(key);
    return err==ERROR_SUCCESS && type==REG_DWORD && n==4 && value==42;
}
static bool binValue(HKEY root) {
    HKEY key=nullptr;
    if(::RegOpenKeyExW(root,L"RATL_P1D_TYPES",0,KEY_READ,&key)!=ERROR_SUCCESS)
        return false;
    BYTE data[8]={};
    DWORD type=0,n=sizeof(data);
    const LSTATUS err=::RegQueryValueExW(key,L"Bytes",nullptr,&type,data,&n);
    ::RegCloseKey(key);
    const BYTE expect[]={0x01,0x02,0xA0,0xFF};
    return err==ERROR_SUCCESS && type==REG_BINARY && n==4 &&
        ::memcmp(data,expect,4)==0;
}
static int fail(int code,const char* stage,HRESULT hr=E_FAIL) {
    std::fprintf(stderr,"RATL_P1D_FAIL code=%d stage=%s hr=0x%08lx\n",
        code,stage,static_cast<unsigned long>(hr));
    return code;
}
static int execute(const char* mode) {
    Sandbox scratch;
    if(!scratch.begin())return fail(10,"scratch_override");
    ATL::CRegObject reg;
    HRESULT hr=E_FAIL;
    bool ok=false;
    const char* tag=nullptr;
    if(::strcmp(mode,"nested")==0) {
        hr=reg.StringRegister(kNested);
        ok=hr==S_OK &&
           str(scratch.key,L"RATL_P1D_TREE\\Child",L"Text",REG_SZ,L"nested");
        if(ok) {
            hr=reg.StringUnregister(kNested);
            ok=hr==S_OK && !exists(scratch.key,L"RATL_P1D_TREE");
        }
        tag="nested";
    } else if(::strcmp(mode,"types")==0) {
        hr=reg.StringRegister(kTypes);
        ok=hr==S_OK &&
            str(scratch.key,L"RATL_P1D_TYPES",L"String",REG_SZ,L"plain") &&
            str(scratch.key,L"RATL_P1D_TYPES",L"Expand",REG_EXPAND_SZ,L"not-expanded") &&
            dwordValue(scratch.key) && binValue(scratch.key);
        if(ok) {
            hr=reg.StringUnregister(kTypes);
            ok=hr==S_OK && !exists(scratch.key,L"RATL_P1D_TYPES");
        }
        tag="types";
    } else if(::strcmp(mode,"noremove")==0) {
        hr=reg.StringRegister(kNoRemove);
        ok=hr==S_OK &&
           str(scratch.key,L"RATL_P1D_KEEP",L"Marker",REG_SZ,L"retained");
        if(ok) {
            hr=reg.StringUnregister(kNoRemove);
            ok=hr==S_OK && exists(scratch.key,L"RATL_P1D_KEEP");
        }
        tag="noremove";
    } else if(::strcmp(mode,"baseline-force")==0 ||
              ::strcmp(mode,"patched-force")==0) {
        if(!seed(scratch.key,L"RATL_P1D_FORCE\\Old\\Deep"))
            return fail(11,"seed_force");
        hr=reg.StringRegister(kForce);
        bool obsolete=exists(scratch.key,L"RATL_P1D_FORCE\\Old\\Deep");
        bool replacement=str(scratch.key,L"RATL_P1D_FORCE",L"New",REG_SZ,L"fresh");
        const bool expectBug=(mode[0]=='b');
        ok=hr==S_OK && replacement && obsolete==expectBug;
        tag=mode;
    } else if(::strcmp(mode,"baseline-delete")==0 ||
              ::strcmp(mode,"patched-delete")==0) {
        if(!seed(scratch.key,L"RATL_P1D_DELETE\\Old\\Deep"))
            return fail(12,"seed_delete");
        hr=reg.StringRegister(kDelete);
        const bool stale=exists(scratch.key,L"RATL_P1D_DELETE\\Old\\Deep");
        const bool expectBug=(mode[0]=='b');
        ok=hr==S_OK && stale==expectBug &&
            exists(scratch.key,L"RATL_P1D_DELETE")==expectBug;
        tag=mode;
    } else if(::strcmp(mode,"denied-force")==0 ||
              ::strcmp(mode,"denied-delete")==0) {
        if(!seed(scratch.key,L"RATL_P1D_DENIED\\Old\\Deep"))
            return fail(16,"seed_access_denied");
        const wchar_t* script=(::strcmp(mode,"denied-force")==0)
            ? kDeniedForce : kDeniedDelete;
        hr=reg.StringRegister(script);
        // Injected ERROR_ACCESS_DENIED must *not* be discarded by
        // ForceRemove/Delete, nor clobber a previously existing subtree.
        ok=hr==HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED) &&
            exists(scratch.key,L"RATL_P1D_DENIED\\Old\\Deep") &&
            !str(scratch.key,L"RATL_P1D_DENIED",L"New",REG_SZ,
                 L"should-not-write");
        tag=mode;
    } else return fail(13,"unknown_mode");
    // Never confuse a test failure with successful cleanup or vice versa.
    const bool cleaned=scratch.cleanup();
    if(!cleaned)return fail(14,"sandbox_cleanup",hr);
    if(!ok)return fail(15,tag,hr);
    if(::strstr(mode,"force") || ::strstr(mode,"delete")) {
        // Explicit evidence of the originally observed HRESULT and
        // whether the nested descendants survived. A baseline that
        // starts behaving differently must fail, not be called repaired.
        const int stale=(::strncmp(mode,"baseline-",9)==0 ||
                         ::strncmp(mode,"denied-",7)==0)?1:0;
        std::printf("RATL_P1D_OK mode=%s hr=0x%08lx stale=%d\n",
                    mode,static_cast<unsigned long>(hr),stale);
    } else {
        std::printf("RATL_P1D_OK mode=%s\n",mode);
    }
    return 0;
}
int main(int argc,char** argv) {
    if(argc!=2)return fail(20,"expected_one_fixed_mode");
    return execute(argv[1]);
}
