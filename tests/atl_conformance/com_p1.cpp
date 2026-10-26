// Isolated OpenATL COM P1 acceptance. Not a full Microsoft ATL conformance suite.
// No registry writes. One-shot local class-object registration is always revoked.
#define WIN32_LEAN_AND_MEAN
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <objidl.h>
#include <ocidl.h>
#include <cstdio>
#include <atlbase.h>
#include <atlcom.h>

#if defined(__RATL__)
#error "OpenATL P1 must use only the pinned OpenATL include tree"
#endif
static_assert(sizeof(void*) == 8, "Windows x64/ARM64 COM ABI required");

// A unique CLSID avoids interference with installed COM classes.
static const CLSID POC_CLSID = {0x7c4d6d6f,0x7e35,0x41ed,{0xb8,0x9a,0x8c,0x27,0x6b,0xef,0x13,0x65}};
class TestModule : public ATL::CAtlModuleT<TestModule> {};
static TestModule module;

class Model : public ATL::CComObjectRootEx<ATL::CComSingleThreadModel>, public IPersist {
public:
    static inline int ctors = 0;
    static inline int destructors = 0;
    static inline int initialized = 0;
    static inline int finals = 0;
    static inline bool reject = false;

    Model() { ++ctors; }
    ~Model() { ++destructors; }

    HRESULT FinalConstruct() noexcept {
        ++initialized;
        return reject ? E_FAIL : S_OK;
    }
    void FinalRelease() noexcept { ++finals; }

    STDMETHOD(GetClassID)(CLSID* out) override {
        if (out == nullptr) return E_POINTER;
        *out = POC_CLSID;
        return S_OK;
    }
    BEGIN_COM_MAP(Model)
        COM_INTERFACE_ENTRY_IID(IID_IPersist, IPersist)
    END_COM_MAP()
    DECLARE_NOT_AGGREGATABLE(Model)

    static void Reset(bool failure) {
        ctors=destructors=initialized=finals=0;
        reject=failure;
    }
};

static int die(int code,const char* what) {
    std::fprintf(stderr,"ATL_COM_P1_FAIL code=%d contract=%s ctor=%d init=%d final=%d dtor=%d\n",
        code,what,Model::ctors,Model::initialized,Model::finals,Model::destructors);
    return code;
}

static HRESULT WINAPI create_model(void* outer, REFIID iid, LPVOID* result) {
    return Model::_CreatorClass::CreateInstance(outer,iid,result);
}

static int model_lifecycle() {
    using Instance=ATL::CComObject<Model>;
    Model::Reset(false);
    Instance* instance=nullptr;
    HRESULT hr=Instance::CreateInstance(&instance);
    if(hr!=S_OK || instance==nullptr || instance->m_dwRef!=0 ||
       Model::ctors!=1 || Model::initialized!=1 || Model::finals!=0)
        return die(11,"successful_construct");
    if(instance->AddRef()!=1) return die(12,"initial_addref");

    IPersist* persist=nullptr;
    if(instance->QueryInterface(IID_IPersist,reinterpret_cast<void**>(&persist))!=S_OK ||
       persist==nullptr || instance->m_dwRef!=2)
        return die(13,"object_qi_identity");

    CLSID id={};
    if(persist->GetClassID(&id)!=S_OK || !IsEqualGUID(id,POC_CLSID))
        return die(14,"object_method");

    void* out=reinterpret_cast<void*>(static_cast<uintptr_t>(1));
    if(instance->QueryInterface(IID_IClassFactory,&out)!=E_NOINTERFACE || out!=nullptr ||
       instance->m_dwRef!=2)
        return die(15,"unsupported_iid_no_leak");
    if(instance->QueryInterface(IID_IPersist,nullptr)!=E_POINTER)
        return die(16,"null_query_out");
    if(persist->Release()!=1 || instance->Release()!=0 ||
       Model::finals!=1 || Model::destructors!=1)
        return die(17,"one_final_release");

    Model::Reset(true);
    Instance* rejected=reinterpret_cast<Instance*>(static_cast<uintptr_t>(1));
    hr=Instance::CreateInstance(&rejected);
    if(hr!=E_FAIL || rejected!=nullptr || Model::ctors!=1 ||
       Model::initialized!=1 || Model::finals!=1 || Model::destructors!=1)
        return die(18,"failure_rolls_back");
    Model::Reset(false);
    return 0;
}

static int class_factory() {
    Model::Reset(false);
    ATL::CComClassFactory factory;
    // The no-function-pointer factory must fail closed.
    void* out=reinterpret_cast<void*>(static_cast<uintptr_t>(1));
    if(factory.CreateInstance(nullptr,IID_IPersist,&out)!=CLASS_E_CLASSNOTAVAILABLE ||
       out!=nullptr)
        return die(30,"unset_factory");

    factory.SetVoid(reinterpret_cast<void*>(&create_model));
    if(factory.QueryInterface(IID_IClassFactory,nullptr)!=E_POINTER)
        return die(31,"factory_qi_null");
    IClassFactory* identity=nullptr;
    if(factory.QueryInterface(IID_IClassFactory,reinterpret_cast<void**>(&identity))!=S_OK ||
       identity!=&factory)
        return die(32,"factory_iid");

    if(factory.CreateInstance(nullptr,IID_IPersist,nullptr)!=E_POINTER)
        return die(33,"factory_null_out");

    IUnknown* outer=static_cast<IClassFactory*>(&factory);
    out=reinterpret_cast<void*>(static_cast<uintptr_t>(1));
    if(factory.CreateInstance(outer,IID_IPersist,&out)!=CLASS_E_NOAGGREGATION || out!=nullptr)
        return die(34,"reject_aggregation_non_iunknown");
    out=reinterpret_cast<void*>(static_cast<uintptr_t>(1));
    if(factory.CreateInstance(outer,IID_IUnknown,&out)!=CLASS_E_NOAGGREGATION || out!=nullptr)
        return die(35,"reject_aggregation_iunknown");

    out=reinterpret_cast<void*>(static_cast<uintptr_t>(1));
    if(factory.CreateInstance(nullptr,IID_IClassFactory,&out)!=E_NOINTERFACE || out!=nullptr ||
       Model::ctors!=1 || Model::finals!=1 || Model::destructors!=1)
        return die(36,"unsupported_factory_interface_rollback");

    IPersist* value=nullptr;
    if(factory.CreateInstance(nullptr,IID_IPersist,reinterpret_cast<void**>(&value))!=S_OK ||
       value==nullptr)
        return die(37,"create_via_class_factory");
    if(value->Release()!=0 ||
       Model::ctors!=2 || Model::finals!=2 || Model::destructors!=2)
        return die(38,"factory_release");

    // IClassFactory::LockServer must balance the module counter.
    const LONG locks=module.GetLockCount();
    if(factory.LockServer(TRUE)!=S_OK || module.GetLockCount()!=locks+1 ||
       factory.LockServer(FALSE)!=S_OK || module.GetLockCount()!=locks)
        return die(39,"module_lock_balance");

    identity->Release();
    return 0;
}

static int activation() {
    // CoRegisterClassObject registers a running class in COM's class table,
    // NOT HKCR/CLSID. Never register or mutate persistent registry here.
    ATL::CComClassFactory factory;
    factory.SetVoid(reinterpret_cast<void*>(&create_model));
    DWORD cookie=0;
    HRESULT hr=::CoRegisterClassObject(POC_CLSID,static_cast<IClassFactory*>(&factory),
        CLSCTX_LOCAL_SERVER,REGCLS_MULTIPLEUSE,&cookie);
    if(hr!=S_OK || cookie==0)
        return die(50,"temporary_class_registration");

    IClassFactory* resolved=nullptr;
    hr=::CoGetClassObject(POC_CLSID,CLSCTX_LOCAL_SERVER,nullptr,IID_IClassFactory,
                          reinterpret_cast<void**>(&resolved));
    if(hr!=S_OK || resolved==nullptr) {
        ::CoRevokeClassObject(cookie);
        return die(51,"running_class_activation");
    }
    IPersist* obj=nullptr;
    hr=resolved->CreateInstance(nullptr,IID_IPersist,reinterpret_cast<void**>(&obj));
    if(hr!=S_OK || obj==nullptr) {
        resolved->Release();
        ::CoRevokeClassObject(cookie);
        return die(52,"registered_factory_instance");
    }
    obj->Release();
    resolved->Release();
    if(::CoRevokeClassObject(cookie)!=S_OK)
        return die(53,"class_registration_revoke");
    // The published class object must be unreachable after revoke.
    IClassFactory* stale=nullptr;
    const HRESULT after=::CoGetClassObject(POC_CLSID,CLSCTX_LOCAL_SERVER,
        nullptr,IID_IClassFactory,reinterpret_cast<void**>(&stale));
    if(SUCCEEDED(after) || stale!=nullptr) {
        if(stale) stale->Release();
        return die(54,"revoked_class_still_visible");
    }
    return 0;
}

int main() {
    HRESULT init=::CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(init)) return die(1,"CoInitializeEx");
    int err=0;
    // A registry API claiming success without registry changes is forbidden.
    if(module.RegisterServer()!=E_NOTIMPL || module.UnregisterServer(FALSE)!=E_NOTIMPL)
        err=die(2,"registry_stub_must_fail_closed");
    if(!err) err=model_lifecycle();
    if(!err) err=class_factory();
    if(!err) err=activation();
    ::CoUninitialize();
    if(err) return err;
    std::puts("ATL_COM_P1_OK create=1 rollback=1 factory=1 nonagg=1 locks=1 running=1 revoke=1 registry_failclosed=1");
    return 0;
}
