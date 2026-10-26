// Independent LGPL RATL conformance reference. Built only with RATL headers.
// No OpenATL headers, WTL GUI objects, or cross-provider C++ ABI sharing.
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <objidl.h>
#include <ocidl.h>
#include <cstdint>
#include <cstdio>
#include <atlbase.h>
#include <atlcom.h>

#ifndef __RATL__
#error "Pinned RATL include-only reference required"
#endif
static_assert(sizeof(void*) == 8, "RATL x64 reference only");

// Never write persistent COM registry state in this fixture.
static const CLSID TEST_CLASS = {
    0x1a7e2f43,0xa65b,0x4c85,{0xa1,0xdd,0x18,0x96,0x5c,0x9a,0x73,0xb2}
};

class Module : public ATL::CAtlModuleT<Module> {};
static Module module;

class Model : public ATL::CComObjectRootEx<ATL::CComSingleThreadModel>,
              public IPersist {
public:
    static inline int created = 0;
    static inline int destroyed = 0;
    static inline int constructed = 0;
    static inline int finalized = 0;
    static inline bool fail = false;

    Model() { ++created; }
    ~Model() { ++destroyed; }

    HRESULT FinalConstruct() {
        ++constructed;
        return fail ? E_FAIL : S_OK;
    }
    void FinalRelease() { ++finalized; }

    STDMETHOD(GetClassID)(CLSID* clsid) override {
        if (!clsid) return E_POINTER;
        *clsid = TEST_CLASS;
        return S_OK;
    }

    BEGIN_COM_MAP(Model)
      COM_INTERFACE_ENTRY_IID(IID_IPersist, IPersist)
    END_COM_MAP()
    DECLARE_NOT_AGGREGATABLE(Model)

    static void Reset(bool failure) {
        created=destroyed=constructed=finalized=0;
        fail=failure;
    }
};

static int bad(int n, const char* reason) {
    std::fprintf(stderr,
        "RATL_COM_P1_FAIL code=%d contract=%s create=%d init=%d final=%d destroy=%d\n",
        n,reason,Model::created,Model::constructed,Model::finalized,Model::destroyed);
    return n;
}

static HRESULT WINAPI create_model(void* outer, REFIID iid, LPVOID* out) {
    return Model::_CreatorClass::CreateInstance(outer,iid,out);
}

static int lifecycle() {
    using Object = ATL::CComObject<Model>;
    Model::Reset(false);
    Object* object=nullptr;
    HRESULT hr=Object::CreateInstance(&object);
    if(hr!=S_OK || !object || object->m_dwRef!=0 ||
       Model::created!=1 || Model::constructed!=1)
        return bad(11,"create_instance");
    if(object->AddRef()!=1) return bad(12,"addref");
    IPersist* iface=nullptr;
    if(object->QueryInterface(IID_IPersist,reinterpret_cast<void**>(&iface))!=S_OK ||
       !iface || object->m_dwRef!=2)
        return bad(13,"query_interface");
    void* unknown=reinterpret_cast<void*>(static_cast<uintptr_t>(1));
    if(object->QueryInterface(IID_IClassFactory,&unknown)!=E_NOINTERFACE ||
       unknown!=nullptr || object->m_dwRef!=2)
        return bad(14,"unknown_iid");
    if(object->QueryInterface(IID_IPersist,nullptr)!=E_POINTER)
        return bad(15,"null_out");
    if(iface->Release()!=1 || object->Release()!=0 ||
       Model::finalized!=1 || Model::destroyed!=1)
        return bad(16,"final_release");

    Model::Reset(true);
    Object* rejected=reinterpret_cast<Object*>(static_cast<uintptr_t>(1));
    hr=Object::CreateInstance(&rejected);
    if(hr!=E_FAIL || rejected!=nullptr || Model::created!=1 ||
       Model::constructed!=1 || Model::finalized!=1 || Model::destroyed!=1)
        return bad(17,"failed_finalconstruct_cleanup");
    Model::Reset(false);
    return 0;
}

// RATL's class factory is used through the *real* RATL CComObject wrapper,
// unlike OpenATL's concrete CComClassFactory. No uninitialized callback call:
// RATL lacks OpenATL's checked missing-callback contract.
using Factory=ATL::CComObject<ATL::CComClassFactory>;
static HRESULT new_factory(Factory** out) {
    *out=nullptr;
    HRESULT hr=Factory::CreateInstance(out);
    if(SUCCEEDED(hr) && *out!=nullptr)
        (*out)->AddRef();  // CComObject::CreateInstance hands back refcount 0
    return hr;
}

static int factory_contract() {
    Model::Reset(false);
    Factory* factory=nullptr;
    if(new_factory(&factory)!=S_OK || !factory)
        return bad(21,"factory_construct");
    factory->SetVoid(reinterpret_cast<void*>(&create_model));

    IClassFactory* iface=nullptr;
    if(factory->QueryInterface(IID_IClassFactory,reinterpret_cast<void**>(&iface))!=S_OK ||
       iface!=static_cast<IClassFactory*>(factory))
        return bad(22,"factory_query_identity");
    if(static_cast<IClassFactory*>(factory)->CreateInstance(nullptr,IID_IPersist,nullptr)!=E_POINTER)
        return bad(23,"factory_null_output");
    IUnknown* outer=static_cast<IClassFactory*>(factory);
    void* result=reinterpret_cast<void*>(static_cast<uintptr_t>(1));
    if(static_cast<IClassFactory*>(factory)->CreateInstance(outer,IID_IPersist,&result)!=CLASS_E_NOAGGREGATION ||
       result!=nullptr)
        return bad(24,"factory_nonagg_interface");
    result=reinterpret_cast<void*>(static_cast<uintptr_t>(1));
    if(static_cast<IClassFactory*>(factory)->CreateInstance(outer,IID_IUnknown,&result)!=CLASS_E_NOAGGREGATION ||
       result!=nullptr)
        return bad(25,"creator_nonaggregation");

    result=reinterpret_cast<void*>(static_cast<uintptr_t>(1));
    if(static_cast<IClassFactory*>(factory)->CreateInstance(nullptr,IID_IClassFactory,&result)!=E_NOINTERFACE ||
       result!=nullptr || Model::created!=1 || Model::destroyed!=1)
        return bad(26,"factory_unknown_iid_rollback");

    IPersist* value=nullptr;
    if(static_cast<IClassFactory*>(factory)->CreateInstance(nullptr,IID_IPersist,
       reinterpret_cast<void**>(&value))!=S_OK || !value)
        return bad(27,"factory_create");
    value->Release();
    if(Model::created!=2 || Model::finalized!=2 || Model::destroyed!=2)
        return bad(28,"factory_destruct");

    const LONG locks=module.GetLockCount();
    if(factory->LockServer(TRUE)!=S_OK || module.GetLockCount()!=locks+1 ||
       factory->LockServer(FALSE)!=S_OK || module.GetLockCount()!=locks)
        return bad(29,"module_locks");

    iface->Release();
    factory->Release();
    return 0;
}

static int class_object() {
    Factory* factory=nullptr;
    if(new_factory(&factory)!=S_OK || !factory)
        return bad(30,"registration_factory");
    factory->SetVoid(reinterpret_cast<void*>(&create_model));
    DWORD cookie=0;
    HRESULT hr=::CoRegisterClassObject(TEST_CLASS,static_cast<IClassFactory*>(factory),
        CLSCTX_LOCAL_SERVER,REGCLS_MULTIPLEUSE,&cookie);
    if(hr!=S_OK || cookie==0) {
        factory->Release();
        return bad(31,"temporary_activation_register");
    }
    IClassFactory* activated=nullptr;
    hr=::CoGetClassObject(TEST_CLASS,CLSCTX_LOCAL_SERVER,nullptr,
                           IID_IClassFactory,reinterpret_cast<void**>(&activated));
    if(hr!=S_OK || !activated) {
        ::CoRevokeClassObject(cookie);
        factory->Release();
        return bad(32,"temporary_activation_lookup");
    }
    IPersist* obj=nullptr;
    hr=activated->CreateInstance(nullptr,IID_IPersist,
                                 reinterpret_cast<void**>(&obj));
    if(hr!=S_OK || !obj) {
        activated->Release();
        ::CoRevokeClassObject(cookie);
        factory->Release();
        return bad(33,"temporary_activation_create");
    }
    obj->Release();
    activated->Release();
    const HRESULT revoked=::CoRevokeClassObject(cookie);
    factory->Release();
    if(revoked!=S_OK) return bad(34,"temporary_activation_revoke");
    IClassFactory* stale=nullptr;
    const HRESULT after=::CoGetClassObject(TEST_CLASS,CLSCTX_LOCAL_SERVER,
        nullptr,IID_IClassFactory,reinterpret_cast<void**>(&stale));
    if(SUCCEEDED(after) || stale!=nullptr) {
        if(stale) stale->Release();
        return bad(35,"revoked_class_still_visible");
    }
    return 0;
}

int main() {
    HRESULT init=::CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(init)) return bad(1,"COM_apartment_init");
    int code=lifecycle();
    if(!code) code=factory_contract();
    if(!code) code=class_object();
    ::CoUninitialize();
    if(code) return code;
    std::puts("RATL_COM_P1_REF_OK create=1 rollback=1 factory=1 nonagg=1 locks=1 running=1 revoke=1");
    return 0;
}
