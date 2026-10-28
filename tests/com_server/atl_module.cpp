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
}

bool RunAtlModuleTests() { return module_locks() && executable(); }
