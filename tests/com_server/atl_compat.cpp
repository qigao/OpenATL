#include <atlcom.h>
#include <atomic>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "ATL_COMPAT_FAIL line=%d: %s\n", __LINE__, #x); return false; } } while (0)
namespace {
inline constexpr IID iidDerived = {0xf9cb25d1, 0x2573, 0x4af4, {0xa4, 0x2e, 0x1e, 0x0a, 0x21, 0xcc, 0x90, 0x20}};
inline constexpr IID iidOther = {0xf9cb25d2, 0x2573, 0x4af4, {0xa4, 0x2e, 0x1e, 0x0a, 0x21, 0xcc, 0x90, 0x20}};
inline constexpr CLSID classId = {0xf9cb25d3, 0x2573, 0x4af4, {0xa4, 0x2e, 0x1e, 0x0a, 0x21, 0xcc, 0x90, 0x20}};
struct IDerived : IPersist {};
struct IOther : IPersist {};
std::atomic<int> live{0}, released{0};
int failure = 0;
OpenATL::COM::Module* observedModule = nullptr;
bool pinnedDuringDestruction = true;

class Compatible : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>,
                   public ATL::CComCoClass<Compatible, &classId>,
                   public IDerived, public IOther, public IClassFactory {
public:
    DECLARE_NOT_AGGREGATABLE(Compatible)
    DECLARE_PROTECT_FINAL_CONSTRUCT()
    BEGIN_COM_MAP(Compatible)
        COM_INTERFACE_ENTRY_IID(iidDerived, IDerived)
        COM_INTERFACE_ENTRY2(IPersist, IDerived)
        COM_INTERFACE_ENTRY_IID(iidOther, IOther)
        COM_INTERFACE_ENTRY(IClassFactory)
    END_COM_MAP()
    Compatible() {
        if (failure == 4) throw std::bad_alloc();
        ++live;
    }
    ~Compatible() {
        if (observedModule && observedModule->CanUnloadNow() != S_FALSE)
            pinnedDuringDestruction = false;
        --live;
    }
    HRESULT FinalConstruct() {
        // A temporary QI/Release during construction must not destroy self.
        ATL::CComPtr<IUnknown> self;
        HRESULT hr = _InternalQueryInterface(IID_IUnknown, reinterpret_cast<void**>(&self));
        if (FAILED(hr)) return hr;
        if (failure == 1) return E_ACCESSDENIED;
        if (failure == 2) throw ATL::CAtlException(E_ABORT);
        if (failure == 3) throw std::runtime_error("FinalConstruct");
        return S_OK;
    }
    void FinalRelease() noexcept { ++released; }
    HRESULT STDMETHODCALLTYPE GetClassID(CLSID* id) override {
        if (!id) return E_POINTER;
        *id = GetObjectCLSID(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown*, REFIID, void** output) override {
        if (!output) return E_POINTER;
        *output = nullptr; return CLASS_E_CLASSNOTAVAILABLE;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL) override { return E_NOTIMPL; }
};

inline constexpr CLSID chainClassId = {0xf9cb25d4, 0x2573, 0x4af4,
    {0xa4, 0x2e, 0x1e, 0x0a, 0x21, 0xcc, 0x90, 0x20}};

// Like Microsoft's COMMap CChainBase/COuter pattern, the derived object
// exposes an outer interface, then chains an inherited base COM map.
// Chaining must adjust MI pointers and never add an extra QI reference.
class ChainBase : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>,
                  public IPersist {
public:
    BEGIN_COM_MAP(ChainBase)
        COM_INTERFACE_ENTRY(IPersist)
    END_COM_MAP()
    HRESULT STDMETHODCALLTYPE GetClassID(CLSID* output) override {
        if (!output) return E_POINTER;
        *output = chainClassId;
        return S_OK;
    }
};
class ChainOuter : public ChainBase, public IClassFactory {
public:
    BEGIN_COM_MAP(ChainOuter)
        COM_INTERFACE_ENTRY(IClassFactory)
        COM_INTERFACE_ENTRY_CHAIN(ChainBase)
    END_COM_MAP()
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown*, REFIID,
                                              void** output) override {
        if (!output) return E_POINTER;
        *output = nullptr;
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL) override { return E_NOTIMPL; }
};
class ChainOnly : public ChainBase {
public:
    BEGIN_COM_MAP(ChainOnly)
        COM_INTERFACE_ENTRY_CHAIN(ChainBase)
    END_COM_MAP()
};
bool ComMapChain() {
    {
        ATL::CComObject<ChainOuter>* raw = nullptr;
        CHECK(ATL::CComObject<ChainOuter>::CreateInstance(&raw) == S_OK);
        CHECK(raw != nullptr && raw->AddRef() == 1);
        ATL::CComPtr<IClassFactory> factory;
        ATL::CComPtr<IPersist> inherited;
        ATL::CComPtr<IUnknown> canonical, chainedIdentity;
        CHECK(raw->QueryInterface(IID_IClassFactory,
              reinterpret_cast<void**>(&factory.p)) == S_OK);
        CHECK(raw->QueryInterface(IID_IPersist,
              reinterpret_cast<void**>(&inherited.p)) == S_OK);
        CHECK(inherited.p == static_cast<IPersist*>(
              static_cast<ChainBase*>(raw)));
        CHECK(static_cast<IUnknown*>(factory.p) !=
              static_cast<IUnknown*>(inherited.p)); // Real MI adjustment.
        CHECK(factory->QueryInterface(IID_IUnknown,
              reinterpret_cast<void**>(&canonical.p)) == S_OK);
        CHECK(inherited->QueryInterface(IID_IUnknown,
              reinterpret_cast<void**>(&chainedIdentity.p)) == S_OK);
        CHECK(canonical.p == chainedIdentity.p);
        CHECK(canonical.p == raw->GetUnknown()); // Outer first entry wins.
        CLSID clsid{};
        CHECK(inherited->GetClassID(&clsid) == S_OK &&
              IsEqualCLSID(clsid, chainClassId));
        void* unsupported = reinterpret_cast<void*>(1);
        CHECK(raw->QueryInterface(IID_IStream, &unsupported) == E_NOINTERFACE &&
              !unsupported);
        CHECK(raw->QueryInterface(IID_IPersist, nullptr) == E_POINTER);
        chainedIdentity.Release();
        canonical.Release();
        inherited.Release();
        factory.Release();
        CHECK(raw->Release() == 0); // Chain did not AddRef twice.
    }
    {
        ATL::CComObject<ChainOnly>* raw = nullptr;
        CHECK(ATL::CComObject<ChainOnly>::CreateInstance(&raw) == S_OK);
        CHECK(raw != nullptr && raw->AddRef() == 1);
        ATL::CComPtr<IPersist> inherited;
        CHECK(raw->QueryInterface(IID_IPersist,
              reinterpret_cast<void**>(&inherited.p)) == S_OK);
        ATL::CComPtr<IUnknown> identity;
        CHECK(raw->QueryInterface(IID_IUnknown,
              reinterpret_cast<void**>(&identity.p)) == S_OK);
        CHECK(identity.p == static_cast<IUnknown*>(inherited.p) &&
              identity.p == raw->GetUnknown());
        identity.Release();
        inherited.Release();
        CHECK(raw->Release() == 0);
    }
    return true;
}
class Single : public ATL::CComObjectRootEx<ATL::CComSingleThreadModel>, public IPersist {
public:
    BEGIN_COM_MAP(Single)
        COM_INTERFACE_ENTRY(IPersist)
    END_COM_MAP()
    HRESULT STDMETHODCALLTYPE GetClassID(CLSID* id) override {
        if (!id) return E_POINTER;
        *id = CLSID_NULL; return S_OK;
    }
};
}

bool RunAtlCompatibilityTests() {
    CHECK(ComMapChain());
    using Object = ATL::CComObject<Compatible>;
    CHECK(Object::CreateInstance(static_cast<Object**>(nullptr)) == E_POINTER);
    Object* object = nullptr;
    CHECK(Object::CreateInstance(&object) == S_OK && object && live == 1);
    CHECK(object->AddRef() == 1); // CreateInstance returns zero, not one.
    object->Lock(); object->Unlock();
    {
        ATL::CComPtr<IClassFactory> other;
        CHECK(object->QueryInterface(IID_IClassFactory, reinterpret_cast<void**>(&other)) == S_OK);
        ATL::CComPtr<IPersist> typed;
        CHECK(object->QueryInterface(&typed.p) == S_OK);
        ATL::CComPtr<IUnknown> a, b;
        CHECK(object->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&a)) == S_OK);
        CHECK(other->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&b)) == S_OK);
        CHECK(a.p == b.p && a.p == object->GetUnknown());
        ATL::CComPtr<IPersist> persist;
        CHECK(other->QueryInterface(IID_IPersist, reinterpret_cast<void**>(&persist)) == S_OK);
        CHECK(persist.p == static_cast<IPersist*>(static_cast<IDerived*>(object)));
        CHECK(static_cast<IPersist*>(static_cast<IOther*>(object)) != persist.p);
        void* output = reinterpret_cast<void*>(1);
        CHECK(object->QueryInterface(IID_IStream, &output) == E_NOINTERFACE && !output);
        CHECK(object->QueryInterface(IID_IUnknown, nullptr) == E_POINTER);
    }
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) threads.emplace_back([object] {
        for (int j = 0; j < 1000; ++j) { object->AddRef(); object->Release(); }
    });
    for (auto& thread : threads) thread.join();
    CHECK(object->Release() == 0 && live == 0 && released == 1);
    const HRESULT errors[] = {S_OK, E_ACCESSDENIED, E_ABORT, E_UNEXPECTED, E_OUTOFMEMORY};
    for (failure = 1; failure <= 4; ++failure) {
        object = reinterpret_cast<Object*>(1);
        CHECK(Object::CreateInstance(&object) == errors[failure] && !object && live == 0);
    }
    CHECK(released == 4); // Failed constructor never calls FinalRelease.
    failure = 0;
    ATL::CComPtr<IPersist> local;
    CHECK((ATL::CComCoClass<Compatible, &classId>::CreateInstance(&local.p)) == S_OK);
    CLSID id{};
    CHECK(local->GetClassID(&id) == S_OK && IsEqualCLSID(id, classId));
    void* output = reinterpret_cast<void*>(1);
    CHECK(Compatible::_CreatorClass::CreateInstance(local.p, IID_IPersist, &output) == CLASS_E_NOAGGREGATION && !output);
    local.Release();
    CHECK(live == 0);
    ATL::CComObject<Single>* single = nullptr;
    CHECK(ATL::CComObject<Single>::CreateInstance(&single) == S_OK);
    CHECK(single->AddRef() == 1 && single->Release() == 0);

    const OpenATL::COM::ClassEntry entries[] = {{classId, &Object::CreateInstance}};
    OpenATL::COM::Module module(entries, 1);
    observedModule = &module;
    ATL::CComPtr<IClassFactory> factory;
    CHECK(module.GetClassObject(classId, IID_IClassFactory, reinterpret_cast<void**>(&factory)) == S_OK);
    CHECK(factory->CreateInstance(nullptr, IID_IPersist, reinterpret_cast<void**>(&local)) == S_OK);
    factory.Release();
    CHECK(module.CanUnloadNow() == S_FALSE && live == 1);
    local.Release();
    CHECK(module.CanUnloadNow() == S_OK && live == 0);
    CHECK(Object::CreateInstance(module, IID_IStream, &output) == E_NOINTERFACE && !output);
    CHECK(module.CanUnloadNow() == S_OK && live == 0);
    failure = 1;
    CHECK(Object::CreateInstance(module, IID_IPersist, &output) == E_ACCESSDENIED && !output);
    CHECK(module.CanUnloadNow() == S_OK && live == 0);
    failure = 0;
    observedModule = nullptr;
    CHECK(pinnedDuringDestruction);
    return true;
}
