// OpenATL P1-B: actual OBJECT_ENTRY_AUTO objects in distinct translation units.
// No HKCR writes, no RATL header mixing, no production module admission.
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <objidl.h>
#include <ocidl.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <atlbase.h>
#include <atlcom.h>

#if defined(__RATL__)
#error "This is only the test-patched OpenATL object-map implementation"
#endif
static_assert(sizeof(void*) == 8, "Windows x64 or ARM64 required");

// Distinct GUIDs from public system classes, used only inside the test process.
const CLSID P1B_CLASS_A = {
    0x1ab0965f,0x76cd,0x48ae,{0xb9,0xa8,0x44,0x21,0x96,0x0a,0x35,0x82}};
extern "C" const CLSID P1B_CLASS_B;

class AutoClassA : public ATL::CComObjectRootEx<ATL::CComSingleThreadModel>,
                   public IPersist {
public:
    BEGIN_COM_MAP(AutoClassA)
        COM_INTERFACE_ENTRY_IID(IID_IPersist, IPersist)
    END_COM_MAP()
    DECLARE_NOT_AGGREGATABLE(AutoClassA)
    STDMETHOD(GetClassID)(CLSID* id) override {
        if (!id) return E_POINTER;
        *id = P1B_CLASS_A;
        return S_OK;
    }
};

OBJECT_ENTRY_AUTO(P1B_CLASS_A, AutoClassA)

static int fail(int code, const char* stage) {
    std::fprintf(stderr, "ATL_COM_P1B_FAIL code=%d stage=%s count=%u\n",
                 code, stage, ATL::AtlAutoObjectCount());
    return code;
}

static bool class_missing(REFCLSID clsid) {
    IClassFactory* ptr = nullptr;
    const HRESULT hr = ::CoGetClassObject(
        clsid, CLSCTX_LOCAL_SERVER, nullptr, IID_IClassFactory,
        reinterpret_cast<void**>(&ptr));
    if(ptr) ptr->Release();
    return FAILED(hr) && ptr == nullptr;
}

static bool activate_one(REFCLSID clsid) {
    IClassFactory* mapFactory=nullptr;
    if (ATL::_AtlComModule.GetClassObject(
         clsid, IID_IClassFactory,
         reinterpret_cast<void**>(&mapFactory)) != S_OK || !mapFactory)
        return false;
    IPersist* fromMap=nullptr;
    HRESULT hr=mapFactory->CreateInstance(
        nullptr,IID_IPersist,reinterpret_cast<void**>(&fromMap));
    mapFactory->Release();
    if(hr!=S_OK || fromMap==nullptr) return false;
    CLSID mapped={};
    bool same=(fromMap->GetClassID(&mapped)==S_OK &&
               ::IsEqualGUID(mapped,clsid));
    fromMap->Release();
    if(!same) return false;

    IClassFactory* classFactory=nullptr;
    hr=::CoGetClassObject(clsid,CLSCTX_LOCAL_SERVER,nullptr,
         IID_IClassFactory,reinterpret_cast<void**>(&classFactory));
    if(hr!=S_OK || !classFactory) return false;
    IPersist* created=nullptr;
    hr=classFactory->CreateInstance(
        nullptr,IID_IPersist,reinterpret_cast<void**>(&created));
    classFactory->Release();
    if(hr!=S_OK || !created) return false;
    CLSID actual={};
    same=(created->GetClassID(&actual)==S_OK && ::IsEqualGUID(actual,clsid));
    created->Release();
    return same;
}

struct WrongThreadState { HRESULT result; };
static DWORD WINAPI wrong_thread_revoke(void* arg) {
    WrongThreadState* state=static_cast<WrongThreadState*>(arg);
    state->result=ATL::_AtlComModule.RevokeClassObjects();
    return 0;
}

// Negative cases run in a FRESH process. A failed automatic-map admission
// sets a sticky fatal error; it must never poison subsequent normal CI runs.
static int negative_map_test(const char* mode) {
    if (ATL::AtlAutoObjectCount()!=2 || ATL::AtlAutoObjectError()!=S_OK)
        return fail(60,"negative_case_precondition");
    HRESULT expected=E_INVALIDARG;
    if(std::strcmp(mode,"invalid")==0) {
        static ATL::_ATL_OBJMAP_ENTRY malformed={};
        if(ATL::AtlRegisterAutoObjectEntry(&malformed))
            return fail(61,"malformed_admitted");
    } else if(std::strcmp(mode,"duplicate")==0) {
        expected=HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
        static ATL::_ATL_OBJMAP_ENTRY repeated=__objMap_AutoClassA;
        if(ATL::AtlRegisterAutoObjectEntry(&repeated))
            return fail(62,"duplicate_class_admitted");
    } else if(std::strcmp(mode,"capacity")==0) {
        expected=HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        // Static storage is essential: module teardown must not outlive any
        // entries added to the bounded process-level map.
        static CLSID ids[15]={};
        static ATL::_ATL_OBJMAP_ENTRY entries[15]={};
        for(unsigned i=0;i<15;i++) {
            ids[i]=P1B_CLASS_A;
            ids[i].Data1+=i+1;
            entries[i]=__objMap_AutoClassA;
            entries[i].pclsid=&ids[i];
            entries[i].dwRegister=0;
            bool accepted=ATL::AtlRegisterAutoObjectEntry(&entries[i]);
            if(i<14 && !accepted) return fail(63,"premature_capacity");
            if(i==14 && accepted) return fail(64,"overflow_admitted");
        }
        if(ATL::AtlAutoObjectCount()!=16)
            return fail(65,"capacity_count");
    } else {
        return fail(66,"unknown_negative_mode");
    }
    if(ATL::AtlAutoObjectError()!=expected)
        return fail(67,"sticky_error_missing");
    void* output=reinterpret_cast<void*>(static_cast<uintptr_t>(1));
    if(ATL::_AtlComModule.GetClassObject(
       P1B_CLASS_A,IID_IClassFactory,&output)!=expected || output!=nullptr)
        return fail(68,"poisoned_map_class_lookup");
    if(ATL::_AtlComModule.RegisterClassObjects(
       CLSCTX_LOCAL_SERVER,REGCLS_MULTIPLEUSE)!=expected)
        return fail(69,"poisoned_map_class_admission");
    std::printf("ATL_COM_P1B_NEG_OK mode=%s\n",mode);
    return 0;
}

int main(int argc,char** argv) {
    if(argc==2) return negative_map_test(argv[1]);
    if(argc!=1) return fail(70,"unexpected_arguments");
    HRESULT init=::CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(init)) return fail(1,"CoInitializeEx");

    if(ATL::AtlAutoObjectError()!=S_OK ||
       ATL::AtlAutoObjectCount()!=2) return fail(10,"two_tu_registration");

    if(ATL::_AtlComModule.GetClassObject(P1B_CLASS_A,
       IID_IClassFactory,nullptr)!=E_POINTER)
        return fail(11,"class_map_null_out");
    void* missing=reinterpret_cast<void*>(static_cast<uintptr_t>(1));
    if(ATL::_AtlComModule.GetClassObject(CLSID_NULL,IID_IClassFactory,
       &missing)!=CLASS_E_CLASSNOTAVAILABLE || missing!=nullptr)
        return fail(12,"unknown_class");
    missing=reinterpret_cast<void*>(static_cast<uintptr_t>(1));
    if(ATL::_AtlComModule.GetClassObject(P1B_CLASS_A,IID_IPersist,
       &missing)!=E_NOINTERFACE || missing!=nullptr)
        return fail(13,"factory_unknown_interface");

    // Inject a failure AFTER entry zero was published. Both classes must
    // be unreachable after the module rolls back the partial registration.
    HRESULT hr=ATL::_AtlComModule.RegisterClassObjects(
        CLSCTX_LOCAL_SERVER,REGCLS_MULTIPLEUSE,1);
    if(hr!=E_OUTOFMEMORY || !class_missing(P1B_CLASS_A) ||
       !class_missing(P1B_CLASS_B) ||
       ATL::_AtlComModule.RevokeClassObjects()!=S_FALSE)
        return fail(20,"atomic_registration_rollback");

    hr=ATL::_AtlComModule.RegisterClassObjects(
        CLSCTX_LOCAL_SERVER,REGCLS_MULTIPLEUSE);
    if(hr!=S_OK) return fail(21,"register_both_classes");
    if(ATL::_AtlComModule.RegisterClassObjects(
        CLSCTX_LOCAL_SERVER,REGCLS_MULTIPLEUSE)!=
           HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS))
        return fail(22,"duplicate_rejected");
    if(!activate_one(P1B_CLASS_A) || !activate_one(P1B_CLASS_B))
        return fail(23,"module_objectmap_class_activation");

    WrongThreadState wrong={S_OK};
    HANDLE t=::CreateThread(nullptr,0,wrong_thread_revoke,&wrong,0,nullptr);
    if(!t) return fail(24,"thread_creation");
    DWORD wait=::WaitForSingleObject(t,5000);
    ::CloseHandle(t);
    if(wait!=WAIT_OBJECT_0 || wrong.result!=RPC_E_WRONG_THREAD)
        return fail(25,"cross_thread_revoke_rejected");

    if(ATL::_AtlComModule.RevokeClassObjects()!=S_OK ||
       !class_missing(P1B_CLASS_A) || !class_missing(P1B_CLASS_B) ||
       ATL::_AtlComModule.RevokeClassObjects()!=S_FALSE)
        return fail(26,"complete_revocation");
    if(ATL::AtlAutoObjectCount()!=2 ||
       ATL::AtlAutoObjectError()!=S_OK)
        return fail(27,"static_object_map_preserved");

    // Registration is process-local. No persistent registry operations.
    ::CoUninitialize();
    std::puts("ATL_COM_P1B_OK map=2 rollback=1 activation=2 revoke=1 wrongthread=1 registry=0");
    return 0;
}
