#include <atlcom.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "ATL_MODULE_FAIL line=%d: %s\n", __LINE__, #x); return false; } } while (0)
namespace {
bool noCom() { ULONG_PTR token = 0; return ::CoGetContextToken(&token) == CO_E_NOTINITIALIZED; }
std::vector<DWORD_PTR> order;
HRESULT lateCallback = S_OK;
void WINAPI record(DWORD_PTR value) {
    order.push_back(value);
    lateCallback = ATL::_pAtlModule->AddTermFunc(record, 3);
}
void WINAPI countCallback(DWORD_PTR value) { ++*reinterpret_cast<std::atomic<int>*>(value); }
bool pinnedInDestructor = true;
inline constexpr CLSID classId = {0xd19b8862, 0xa881, 0x47f6, {0x9c, 0x71, 0x91, 0xb2, 0xaf, 0x43, 0xe1, 0x03}};
inline constexpr CLSID autoClassId = {0xd19b8863, 0xa881, 0x47f6, {0x9c, 0x71, 0x91, 0xb2, 0xaf, 0x43, 0xe1, 0x03}};
std::atomic<int> autoLive{0}, autoFinalRelease{0};

class Value : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>, public IPersist {
public:
    BEGIN_COM_MAP(Value)
        COM_INTERFACE_ENTRY(IPersist)
    END_COM_MAP()
    ~Value() {
        if (!ATL::_pAtlModule || ATL::_pAtlModule->GetLockCount() == 0) pinnedInDestructor = false;
    }
    HRESULT STDMETHODCALLTYPE GetClassID(CLSID* output) override {
        if (!output) return E_POINTER;
        *output = classId; return S_OK;
    }
};
// The unchanged client-side ATL OBJECT_ENTRY_AUTO spelling registers a
// binary-local class. The generated factory uses the *same* COM module
// provider as explicit class entries and requires no manual BindServer.
class AutoValue : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>,
                  public ATL::CComCoClass<AutoValue, &autoClassId>, public IPersist {
public:
    BEGIN_COM_MAP(AutoValue)
        COM_INTERFACE_ENTRY(IPersist)
    END_COM_MAP()
    AutoValue() noexcept { ++autoLive; }
    ~AutoValue() { --autoLive; }
    void FinalRelease() noexcept { ++autoFinalRelease; }
    HRESULT STDMETHODCALLTYPE GetClassID(CLSID* output) override {
        if (!output) return E_POINTER;
        *output = autoClassId;
        return S_OK;
    }
};
OBJECT_ENTRY_AUTO(autoClassId, AutoValue)

class Factory : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>, public IClassFactory {
public:
    BEGIN_COM_MAP(Factory)
        COM_INTERFACE_ENTRY(IClassFactory)
    END_COM_MAP()
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID iid, void** output) override {
        return ATL::CComCreator<ATL::CComObject<Value>>::CreateInstance(outer, iid, output);
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override {
        if (lock) ATL::_pAtlModule->Lock();
        else ATL::_pAtlModule->Unlock();
        return S_OK;
    }
};
class Executable : public ATL::CAtlExeModuleT<Executable> {
public:
    HRESULT observed = E_UNEXPECTED;
    bool throwInLoop = false;
    int registered = 0, unregistered = 0;
    HRESULT RegisterServer(BOOL library, const CLSID* = nullptr) noexcept {
        if (!library) return E_INVALIDARG;
        ++registered; return S_OK; // Test hook; persistent registration is not invoked.
    }
    HRESULT UnregisterServer(BOOL library, const CLSID* = nullptr) noexcept {
        if (!library) return E_INVALIDARG;
        ++unregistered; return S_OK;
    }
    void RunMessageLoop() {
        if (throwInLoop) throw ATL::CAtlException(E_ABORT);
        ATL::CComPtr<IClassFactory> factory;
        observed = ::CoGetClassObject(classId, CLSCTX_LOCAL_SERVER, nullptr,
            IID_IClassFactory, reinterpret_cast<void**>(&factory));
        if (SUCCEEDED(observed)) {
            ATL::CComPtr<IPersist> value;
            observed = factory->CreateInstance(nullptr, IID_IPersist, reinterpret_cast<void**>(&value));
            if (SUCCEEDED(observed)) {
                CLSID actual{};
                observed = value->GetClassID(&actual);
                if (!IsEqualCLSID(actual, classId)) observed = E_UNEXPECTED;
                factory->LockServer(TRUE);
                factory->LockServer(FALSE);
            }
        }
        ::PostQuitMessage(0);
        ATL::CAtlExeModuleT<Executable>::RunMessageLoop();
    }
};
class Plain : public ATL::CAtlExeModuleT<Plain> {};
class ThrowsAfterInit : public ATL::CAtlExeModuleT<ThrowsAfterInit> {
public:
    ThrowsAfterInit() { throw std::bad_alloc(); }
};
class FailingPre : public ATL::CAtlExeModuleT<FailingPre> {
public:
    int loops = 0;
    HRESULT PreMessageLoop(int) noexcept { return E_ACCESSDENIED; }
    void RunMessageLoop() noexcept { ++loops; }
};
int initializations = 0, uninitializations = 0;
class CustomSta : public ATL::CAtlExeModuleT<CustomSta> {
public:
    static HRESULT InitializeCom() noexcept {
        ++initializations; return ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    }
    static void UninitializeCom() noexcept { ++uninitializations; ::CoUninitialize(); }
};

bool module_locks() {
    CHECK(noCom() && !ATL::_pAtlModule);
    {
        ATL::CAtlModule module;
        CHECK(noCom() && ATL::_pAtlModule == &module && module.GetLockCount() == 0);
        CHECK(module.AddTermFunc(nullptr, 0) == E_INVALIDARG);
        CHECK(module.AddTermFunc(record, 1) == S_OK && module.AddTermFunc(record, 2) == S_OK);
        CHECK(module.Lock() == 1 && module.Unlock() == 0);
        std::vector<std::thread> threads;
        for (int i = 0; i < 4; ++i) threads.emplace_back([&] {
            for (int n = 0; n < 1000; ++n) { module.Lock(); module.Unlock(); }
        });
        for (auto& thread : threads) thread.join();
        CHECK(module.GetLockCount() == 0);
        module.Term(); module.Term();
        CHECK(order == std::vector<DWORD_PTR>({2, 1}) && lateCallback == E_UNEXPECTED);
    }
    CHECK(!ATL::_pAtlModule && noCom());
    std::atomic<int> callbacks{0};
    {
        ATL::CAtlModule module;
        for (int i = 0; i < 1024; ++i)
            CHECK(module.AddTermFunc(countCallback, reinterpret_cast<DWORD_PTR>(&callbacks)) == S_OK);
        CHECK(module.AddTermFunc(countCallback, reinterpret_cast<DWORD_PTR>(&callbacks)) ==
              HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW));
    }
    CHECK(callbacks == 1024);
    return true;
}
bool executable() {
    CHECK(noCom());
    {
        Executable module;
        CHECK(module.GetInitHRESULT() == S_OK && !noCom());
        ATL::CComObject<Value>* value = nullptr;
        CHECK(ATL::CComObject<Value>::CreateInstance(&value) == S_OK);
        CHECK(module.GetLockCount() == 1 && value->AddRef() == 1);
        CHECK(value->Release() == 0 && module.GetLockCount() == 0 && pinnedInDestructor);
        CHECK(module.Run() == S_FALSE && module.RevokeClassObjects() == S_FALSE);
        HRESULT other = S_OK;
        std::thread wrong([&] { other = module.Run(); }); wrong.join();
        CHECK(other == RPC_E_WRONG_THREAD);
        bool wrongParse = true;
        std::thread wrongCommand([&] {
            wrongParse = module.ParseCommandLine(L"app.exe /RegServer", &other);
        });
        wrongCommand.join();
        CHECK(!wrongParse && other == RPC_E_WRONG_THREAD && module.registered == 0);
        HRESULT parsed = E_FAIL;
        CHECK(module.ParseCommandLine(L"\"C:\\RegServer folder\\app.exe\" /Embedding", &parsed) && parsed == S_OK);
        CHECK(!module.ParseCommandLine(L"app.exe /regserver", &parsed) && parsed == S_OK && module.registered == 1);
        CHECK(!module.ParseCommandLine(L"app.exe -UnregServer", &parsed) && parsed == S_OK && module.unregistered == 1);
        CHECK(!module.ParseCommandLine(nullptr, &parsed) && parsed == E_INVALIDARG);
        const std::wstring tooLong(32767, L'x');
        CHECK(!module.ParseCommandLine(tooLong.c_str(), &parsed) &&
              parsed == HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW));

        ATL::CComObject<Factory>* raw = nullptr;
        CHECK(ATL::CComObject<Factory>::CreateInstance(&raw) == S_OK);
        raw->AddRef();
        ATL::CComPtr<IClassFactory> factory;
        factory.Attach(raw);
        const OpenATL::COM::ClassObjectEntry entries[] = {{classId, factory.p}};
        CHECK(module.SetClassObjects(nullptr, 1) == E_INVALIDARG);
        CHECK(module.SetClassObjects(entries, 1) == S_OK);
        const OpenATL::COM::ClassObjectEntry duplicates[] = {entries[0], entries[0]};
        CHECK(module.SetClassObjects(duplicates, 2) == E_INVALIDARG);
        std::array<OpenATL::COM::ClassObjectEntry, 1025> tooMany{};
        CHECK(module.SetClassObjects(tooMany.data(), tooMany.size()) == HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW));
        CHECK(module.RegisterClassObjects(CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE) == S_OK);
        CHECK(module.RegisterClassObjects(CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE) == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS));
        CHECK(module.SetClassObjects(nullptr, 0) == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS));
        CHECK(module.RevokeClassObjects() == S_OK && module.RevokeClassObjects() == S_FALSE);
        CHECK(module.Run() == S_OK && module.observed == S_OK);
        module.throwInLoop = true;
        CHECK(module.Run() == E_ABORT && module.RevokeClassObjects() == S_FALSE);
        CHECK(module.SetClassObjects(nullptr, 0) == S_OK);
        factory.Release();
        CHECK(module.GetLockCount() == 0 && pinnedInDestructor);
    }
    CHECK(noCom() && !ATL::_pAtlModule);
    bool constructorFailed = false;
    try { ThrowsAfterInit module; }
    catch (const std::bad_alloc&) { constructorFailed = true; }
    CHECK(constructorFailed && noCom() && !ATL::_pAtlModule);
    {
        Plain module;
        HRESULT result = S_OK;
        CHECK(!module.ParseCommandLine(L"app.exe /RegServer", &result) && result == E_NOTIMPL);
        CHECK(!module.ParseCommandLine(L"app.exe /RegServerPerUser", &result) && result == E_NOTIMPL);
        CHECK(module.WinMain(SW_HIDE) == static_cast<int>(S_FALSE));
    }
    {
        FailingPre module;
        CHECK(module.Run() == E_ACCESSDENIED && module.loops == 0);
    }
    CHECK(noCom());
    CHECK(::CoInitializeEx(nullptr, COINIT_MULTITHREADED) == S_OK);
    {
        Plain module;
        CHECK(module.GetInitHRESULT() == S_FALSE);
    }
    CHECK(!noCom());
    ::CoUninitialize();
    CHECK(noCom());
    CHECK(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED) == S_OK);
    {
        Plain module;
        CHECK(module.GetInitHRESULT() == RPC_E_CHANGED_MODE && module.Run() == RPC_E_CHANGED_MODE);
    }
    APTTYPE type; APTTYPEQUALIFIER qualifier;
    CHECK(::CoGetApartmentType(&type, &qualifier) == S_OK &&
          (type == APTTYPE_STA || type == APTTYPE_MAINSTA));
    ::CoUninitialize();
    {
        CustomSta module;
        CHECK(module.GetInitHRESULT() == S_OK);
        CHECK(::CoGetApartmentType(&type, &qualifier) == S_OK &&
              (type == APTTYPE_STA || type == APTTYPE_MAINSTA));
    }
    CHECK(initializations == 1 && uninitializations == 1 && noCom());
    return true;
}

// Standard ATL DLL-module entry points use the same real factory/owner
// as the explicit OpenATL::COM server. No COM apartment is initialized
// by constructing this module or querying its class factory.
class Library : public ATL::CAtlDllModuleT<Library> {};
class RegistrationProbe : public ATL::CAtlDllModuleT<RegistrationProbe> {
public:
    int installs = 0, removals = 0;
    HRESULT RegisterServer(BOOL withTypeLib, const CLSID* selected) noexcept {
        if (withTypeLib || selected || GetLockCount() != 1) return E_UNEXPECTED;
        ++installs; return E_ACCESSDENIED;
    }
    HRESULT UnregisterServer(BOOL withTypeLib, const CLSID* selected) noexcept {
        if (withTypeLib || selected || GetLockCount() != 1) return E_UNEXPECTED;
        ++removals; return E_ABORT;
    }
};
class HookedLookup : public ATL::CAtlDllModuleT<HookedLookup> {
public:
    int callbacks = 0;
    HRESULT GetClassObject(REFCLSID, REFIID, LPVOID* output) noexcept {
        ++callbacks;
        if (!output || GetLockCount() != 1) return E_UNEXPECTED;
        *output = nullptr;
        return E_ACCESSDENIED;
    }
};
class NullLookup : public ATL::CAtlDllModuleT<NullLookup> {
public:
    HRESULT GetClassObject(REFCLSID, REFIID, LPVOID* output) noexcept {
        if (!output || GetLockCount() != 1) return E_UNEXPECTED;
        *output = nullptr;
        return S_OK; // Invalid custom hook: export must fail closed.
    }
};
HRESULT makeDllValue(OpenATL::COM::Module& server, REFIID iid, void** output) noexcept {
    return ATL::CComObject<Value>::CreateInstance(server, iid, output);
}
bool dll_module() {
    CHECK(noCom() && ATL::_pAtlModule == nullptr);
    OpenATL::COM::Module invalid(nullptr, 1);
    const OpenATL::COM::ClassEntry entries[] = {{classId, &makeDllValue}};
    OpenATL::COM::Module server(entries, 1);
    OpenATL::COM::Module alternate(entries, 1);
    CHECK(server.Status() == S_OK && alternate.Status() == S_OK);
    {
        Library dll;
        CHECK(ATL::_pAtlModule == &dll && noCom());
        CHECK(dll.DllMain(DLL_PROCESS_ATTACH, nullptr) && noCom());
        CHECK(dll.DllMain(DLL_PROCESS_DETACH, nullptr) && noCom());
        CHECK(dll.DllCanUnloadNow() == S_OK);
        void* output = reinterpret_cast<void*>(1);
        CHECK(dll.DllGetClassObject(classId, IID_IClassFactory, nullptr) == E_POINTER);
        CHECK(dll.DllGetClassObject(classId, IID_IClassFactory, &output) ==
              CLASS_E_CLASSNOTAVAILABLE && !output);
        CHECK(dll.BindServer(invalid) == E_INVALIDARG);
        CHECK(dll.BindServer(server) == S_OK && dll.BindServer(server) == S_OK);
        CHECK(dll.BindServer(alternate) == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS));

        output = reinterpret_cast<void*>(1);
        CHECK(dll.DllGetClassObject(CLSID_NULL, IID_IClassFactory, &output) ==
              CLASS_E_CLASSNOTAVAILABLE && !output);
        CHECK(dll.DllGetClassObject(classId, IID_IStream, &output) ==
              E_NOINTERFACE && !output);
        CHECK(dll.DllRegisterServer(TRUE) == E_NOTIMPL);
        CHECK(dll.DllUnregisterServer(TRUE) == E_NOTIMPL);

        ATL::CComPtr<IClassFactory> factory;
        CHECK(dll.GetClassObject(classId, IID_IClassFactory,
              reinterpret_cast<void**>(&factory.p)) == S_OK);
        CHECK(dll.DllCanUnloadNow() == S_FALSE);
        ATL::CComPtr<IPersist> value;
        CHECK(factory->CreateInstance(nullptr, IID_IPersist,
              reinterpret_cast<void**>(&value.p)) == S_OK);
        CHECK(value.p != nullptr && dll.DllCanUnloadNow() == S_FALSE);
        CLSID actual{};
        CHECK(value->GetClassID(&actual) == S_OK && IsEqualCLSID(actual, classId));
        CHECK(factory->LockServer(TRUE) == S_OK);
        value.Release();
        CHECK(factory->LockServer(FALSE) == S_OK);
        CHECK(dll.DllCanUnloadNow() == S_FALSE); // Factory still live.
        factory.Release();
        CHECK(dll.DllCanUnloadNow() == S_OK && server.CanUnloadNow() == S_OK);
        CHECK(dll.Lock() == 1 && dll.DllCanUnloadNow() == S_FALSE);
        CHECK(dll.Unlock() == 0 && dll.DllCanUnloadNow() == S_OK);
        CHECK(dll.GetLockCount() == 0 && pinnedInDestructor);
    }
    CHECK(noCom() && ATL::_pAtlModule == nullptr &&
          server.CanUnloadNow() == S_OK);
    {
        RegistrationProbe dll;
        CHECK(dll.DllRegisterServer(FALSE) == E_ACCESSDENIED &&
              dll.DllUnregisterServer(FALSE) == E_ABORT);
        CHECK(dll.installs == 1 && dll.removals == 1 &&
              dll.GetLockCount() == 0 && dll.DllCanUnloadNow() == S_OK);
    }
    {
        HookedLookup dll;
        void* output = reinterpret_cast<void*>(1);
        CHECK(dll.DllGetClassObject(classId, IID_IClassFactory, &output) ==
              E_ACCESSDENIED && !output && dll.callbacks == 1);
        CHECK(dll.GetLockCount() == 0 && dll.DllCanUnloadNow() == S_OK);
    }
    {
        NullLookup dll;
        void* output = reinterpret_cast<void*>(1);
        CHECK(dll.DllGetClassObject(classId, IID_IClassFactory, &output) ==
              E_UNEXPECTED && !output && dll.GetLockCount() == 0);
    }
    CHECK(noCom() && ATL::_pAtlModule == nullptr);
    return true;
}
}

namespace {
bool auto_object_map() {
    CHECK(noCom() && !ATL::_pAtlModule && autoLive == 0);
    {
        Library dll;
        CHECK(dll.DllCanUnloadNow() == S_OK);
        ATL::CComPtr<IClassFactory> factory;
        CHECK(dll.DllGetClassObject(autoClassId, IID_IClassFactory,
            reinterpret_cast<void**>(&factory.p)) == S_OK && factory.p);
        CHECK(dll.DllCanUnloadNow() == S_FALSE && dll.GetLockCount() == 0);
        void* unexpected = reinterpret_cast<void*>(1);
        CHECK(dll.DllGetClassObject(CLSID_NULL, IID_IClassFactory, &unexpected) ==
              CLASS_E_CLASSNOTAVAILABLE && !unexpected);
        CHECK(dll.DllGetClassObject(autoClassId, IID_IStream, &unexpected) ==
              E_NOINTERFACE && !unexpected);
        CHECK(factory->CreateInstance(reinterpret_cast<IUnknown*>(1),
              IID_IPersist, &unexpected) == CLASS_E_NOAGGREGATION && !unexpected);
        ATL::CComPtr<IPersist> object;
        CHECK(factory->CreateInstance(nullptr, IID_IPersist,
              reinterpret_cast<void**>(&object.p)) == S_OK);
        CHECK(autoLive == 1 && dll.DllCanUnloadNow() == S_FALSE);
        CLSID actual{};
        CHECK(object->GetClassID(&actual) == S_OK && IsEqualCLSID(actual, autoClassId));
        CHECK(factory->LockServer(FALSE) == E_UNEXPECTED);
        CHECK(factory->LockServer(TRUE) == S_OK);
        factory.Release();
        object.Release();
        CHECK(autoLive == 0 && autoFinalRelease == 1 &&
              dll.DllCanUnloadNow() == S_FALSE);
        // Server lock is per module, not per factory pointer. A different
        // factory can balance it; no COM Server implementation is duplicated.
        CHECK(dll.DllGetClassObject(autoClassId, IID_IClassFactory,
              reinterpret_cast<void**>(&factory.p)) == S_OK);
        CHECK(factory->LockServer(FALSE) == S_OK);
        factory.Release();
        CHECK(dll.DllCanUnloadNow() == S_OK && dll.GetLockCount() == 0);
    }
    CHECK(noCom() && !ATL::_pAtlModule && autoLive == 0);
    return true;
}

bool duplicate_auto_map() {
    // Inject an actual duplicate after the positive auto-map test. Each DLL
    // instance freezes its map exactly once, so a new owner must reject this
    // ambiguous pair and clear the out pointer rather than picking one.
    static ATL::detail::AutoObjectEntry duplicate(
        autoClassId, static_cast<OpenATL::COM::Creator>(
            &ATL::CComObject<AutoValue>::CreateInstance));
    CHECK(noCom() && !ATL::_pAtlModule);
    {
        Library dll;
        void* out = reinterpret_cast<void*>(1);
        CHECK(dll.DllGetClassObject(autoClassId, IID_IClassFactory, &out) ==
              E_INVALIDARG && !out);
        CHECK(dll.DllCanUnloadNow() == S_OK && dll.GetLockCount() == 0);
    }
    CHECK(noCom() && !ATL::_pAtlModule && autoLive == 0);
    return true;
}
}

bool RunAtlModuleTests() {
    return module_locks() && executable() && dll_module() &&
           auto_object_map() && duplicate_auto_map();
}
