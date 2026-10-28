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
