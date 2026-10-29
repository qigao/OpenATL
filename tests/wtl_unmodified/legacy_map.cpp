// Original ATL _ATL_OBJMAP_ENTRY registration against actual Win32 COM.
// This test is compiled into the original-vcpkg-WTL direct-header executable,
// but does not substitute Microsoft ATL or a second factory implementation.
#include <atlbase.h>
#include <atlcom.h>
#include <objidl.h>
#include <atomic>
#include <cstdio>
#include <new>
#include <thread>

#define EXPECT(x) do { if (!(x)) { \
  std::fprintf(stderr, "LEGACY_OBJMAP_FAIL line=%d: %s\n", __LINE__, #x); \
  return false; } } while (0)

namespace {
constexpr CLSID kLegacyA = {0xbd6aa04e,0x6d91,0x492d,{0xb2,0x1c,0xfb,0x0c,0x3e,0xb8,0x6b,0xa1}};
constexpr CLSID kLegacyB = {0xbd6aa04f,0x6d91,0x492d,{0xb2,0x1c,0xfb,0x0c,0x3e,0xb8,0x6b,0xa1}};
std::atomic<int> started{0}, stopped{0}, factoryLive{0}, objectLive{0};

class Persist final : public IPersist {
public:
    Persist() { ++objectLive; }
    ~Persist() { --objectLive; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) noexcept override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (!::IsEqualIID(iid, IID_IUnknown) && !::IsEqualIID(iid, IID_IPersist))
            return E_NOINTERFACE;
        *out = static_cast<IPersist*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() noexcept override {
        const ULONG refs = --refs_;
        if (!refs) delete this;
        return refs;
    }
    HRESULT STDMETHODCALLTYPE GetClassID(CLSID* id) noexcept override {
        if (!id) return E_POINTER;
        *id = kLegacyA;
        return S_OK;
    }
private:
    std::atomic<ULONG> refs_{1};
};
HRESULT WINAPI CreatePersist(void*, REFIID iid, LPVOID* out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    Persist* object = new (std::nothrow) Persist;
    if (!object) return E_OUTOFMEMORY;
    HRESULT hr = object->QueryInterface(iid, out);
    object->Release();
    return hr;
}

class Factory final : public IClassFactory {
public:
    Factory() { ++factoryLive; }
    ~Factory() { --factoryLive; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) noexcept override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (!::IsEqualIID(iid, IID_IUnknown) &&
            !::IsEqualIID(iid, IID_IClassFactory)) return E_NOINTERFACE;
        *out = static_cast<IClassFactory*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() noexcept override {
        const ULONG refs = --refs_;
        if (!refs) delete this;
        return refs;
    }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID iid,
                                             void** out) noexcept override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        return CreatePersist(nullptr, iid, out);
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) noexcept override {
        ATL::CAtlModule* module = ATL::_pAtlModule;
        if (!module) return E_UNEXPECTED;
        if (lock) module->Lock();
        else module->Unlock();
        return S_OK;
    }
private:
    std::atomic<ULONG> refs_{1};
};
HRESULT WINAPI GetFactory(void* creator, REFIID iid, LPVOID* out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (!creator) return E_INVALIDARG;
    Factory* factory = new (std::nothrow) Factory;
    if (!factory) return E_OUTOFMEMORY;
    HRESULT hr = factory->QueryInterface(iid, out);
    factory->Release();
    return hr;
}
HRESULT WINAPI FailFactory(void*, REFIID, LPVOID* out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    return E_ACCESSDENIED;
}
void WINAPI OnObjectMain(bool starting) {
    if (starting) ++started;
    else ++stopped;
}
struct CoApartment {
    HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~CoApartment() { if (SUCCEEDED(hr)) ::CoUninitialize(); }
};
bool NoCom() {
    ULONG_PTR token = 0;
    return ::CoGetContextToken(&token) == CO_E_NOTINITIALIZED;
}
bool OriginalMapRuntime() {
    EXPECT(NoCom());
    HINSTANCE image = ::GetModuleHandleW(nullptr);
    EXPECT(image != nullptr);
    // Reject unsafe/incomplete object tables before changing the global
    // module state, rather than claiming to have registered a COM server.
    {
        ATL::CComModule module;
        ATL::_ATL_OBJMAP_ENTRY incomplete[] = {
            {&kLegacyA, nullptr, nullptr, &CreatePersist}, {}
        };
        EXPECT(module.Init(incomplete, image) == E_INVALIDARG);
        const ATL::_ATL_OBJMAP_ENTRY duplicate[] = {
            {&kLegacyA, nullptr, &GetFactory, &CreatePersist},
            {&kLegacyA, nullptr, &GetFactory, &CreatePersist}, {}
        };
        // The legacy ABI owns a mutable registration-cookie table.
        auto validDuplicate = duplicate;
        EXPECT(module.Init(validDuplicate, image) == E_INVALIDARG);
        EXPECT(module.GetLockCount() == 0);
    }
    EXPECT(NoCom());
    {
        CoApartment apartment;
        EXPECT(SUCCEEDED(apartment.hr));
        EXPECT(started == 0 && stopped == 0);
        {
            ATL::CComModule module;
            ATL::_ATL_OBJMAP_ENTRY map[] = {
                {&kLegacyA, nullptr, &GetFactory, &CreatePersist,
                 nullptr, 0, nullptr, nullptr, &OnObjectMain}, {}
            };
            EXPECT(module.RegisterClassObjects(CLSCTX_LOCAL_SERVER,
                                               REGCLS_MULTIPLEUSE) == E_UNEXPECTED);
            EXPECT(module.Init(map, image) == S_OK && started == 1);
            EXPECT(module.m_pObjMap == map && module.GetLockCount() == 0);
            EXPECT(module.RegisterClassObjects(CLSCTX_LOCAL_SERVER,
                                               REGCLS_MULTIPLEUSE) == S_OK);
            EXPECT(map[0].dwRegister != 0 && map[0].pCF && factoryLive >= 1);
            EXPECT(module.RegisterClassObjects(CLSCTX_LOCAL_SERVER,
                                               REGCLS_MULTIPLEUSE) ==
                   HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS));
            HRESULT wrongThread = E_UNEXPECTED;
            std::thread worker([&] { wrongThread = module.RevokeClassObjects(); });
            worker.join();
            EXPECT(wrongThread == RPC_E_WRONG_THREAD);
            ATL::CComPtr<IClassFactory> factory;
            EXPECT(::CoGetClassObject(kLegacyA, CLSCTX_LOCAL_SERVER, nullptr,
                   IID_IClassFactory, reinterpret_cast<void**>(&factory)) == S_OK);
            ATL::CComPtr<IPersist> object;
            EXPECT(factory->CreateInstance(nullptr, IID_IPersist,
                   reinterpret_cast<void**>(&object)) == S_OK);
            CLSID actual{};
            EXPECT(object->GetClassID(&actual) == S_OK &&
                   ::IsEqualCLSID(actual, kLegacyA));
            EXPECT(factory->LockServer(TRUE) == S_OK && module.GetLockCount() == 1);
            EXPECT(factory->LockServer(FALSE) == S_OK && module.GetLockCount() == 0);
            object.Release();
            factory.Release();
            EXPECT(objectLive == 0);
            EXPECT(module.RevokeClassObjects() == S_OK);
            EXPECT(map[0].dwRegister == 0 && map[0].pCF == nullptr);
            EXPECT(module.RevokeClassObjects() == S_FALSE);
            module.Term();
            EXPECT(stopped == 1 && factoryLive == 0);
        }
        {
            ATL::CComModule module;
            ATL::_ATL_OBJMAP_ENTRY map[] = {
                {&kLegacyA, nullptr, &GetFactory, &CreatePersist},
                {&kLegacyB, nullptr, &FailFactory, &CreatePersist}, {}
            };
            EXPECT(module.Init(map, image) == S_OK);
            EXPECT(module.RegisterClassObjects(CLSCTX_LOCAL_SERVER,
                                               REGCLS_MULTIPLEUSE) == E_ACCESSDENIED);
            EXPECT(map[0].dwRegister == 0 && map[0].pCF == nullptr);
            EXPECT(map[1].dwRegister == 0 && map[1].pCF == nullptr);
            EXPECT(module.RevokeClassObjects() == S_FALSE);
            module.Term();
        }
        EXPECT(factoryLive == 0 && objectLive == 0 &&
               started == 1 && stopped == 1);
    }
    EXPECT(NoCom());
    return true;
}
} // namespace
bool RunLegacyObjectMapTests() {
    const bool okay = OriginalMapRuntime();
    if (okay)
        std::puts("OPENATL_WTL_ORIGINAL_MAP_OK factory=1 activate=1 cookies=1 rollback=1 thread=1 no_com=1");
    return okay;
}
