#include <atlcom.h>
#include <statreg.h>
#include "fixture_registry_ids.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>
#include <type_traits>
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
struct ModuleRegistryRoot {
    ATL::CRegKey key;
    std::wstring path;
    bool owned = false;
    bool Open() {
        GUID guid{};
        wchar_t text[40]{};
        if (FAILED(::CoCreateGuid(&guid)) || !::StringFromGUID2(guid, text, 40)) return false;
        path = std::wstring(L"Software\\OpenATL.ModuleRegistryTest.") + text;
        DWORD disposition = 0;
        const LSTATUS result = key.Create(HKEY_CURRENT_USER, path.c_str(), nullptr,
            REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr, &disposition);
        owned = result == ERROR_SUCCESS && disposition == REG_CREATED_NEW_KEY;
        return owned;
    }
    ~ModuleRegistryRoot() {
        key.Close();
        if (owned) ::RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str());
    }
};
bool pinnedInDestructor = true;
inline constexpr CLSID classId = {0xd19b8862, 0xa881, 0x47f6, {0x9c, 0x71, 0x91, 0xb2, 0xaf, 0x43, 0xe1, 0x03}};
inline constexpr CLSID autoClassId = {0xd19b8863, 0xa881, 0x47f6, {0x9c, 0x71, 0x91, 0xb2, 0xaf, 0x43, 0xe1, 0x03}};
inline constexpr CLSID noRegistryClassId = {0xd19b8864, 0xa881, 0x47f6, {0x9c, 0x71, 0x91, 0xb2, 0xaf, 0x43, 0xe1, 0x03}};
inline constexpr CLSID registrationClassId = {0xd19b8865, 0xa881, 0x47f6, {0x9c, 0x71, 0x91, 0xb2, 0xaf, 0x43, 0xe1, 0x03}};
inline constexpr CLSID exeRegistryClassId = {0xd19b8866, 0xa881, 0x47f6, {0x9c, 0x71, 0x91, 0xb2, 0xaf, 0x43, 0xe1, 0x03}};
int registrationCalls = 0;
int exeRegistryCalls = 0;
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

// An explicit DECLARE_NO_REGISTRY is the sole intentional no-op registrar.
// A class without UpdateRegistry must instead fail with E_NOTIMPL.
class NoRegistryValue : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>,
                        public ATL::CComCoClass<NoRegistryValue, &noRegistryClassId>,
                        public IPersist {
public:
    DECLARE_NO_REGISTRY()
    BEGIN_COM_MAP(NoRegistryValue)
        COM_INTERFACE_ENTRY(IPersist)
    END_COM_MAP()
    HRESULT STDMETHODCALLTYPE GetClassID(CLSID* output) override {
        if (!output) return E_POINTER;
        *output = noRegistryClassId;
        return S_OK;
    }
};
OBJECT_ENTRY_AUTO(noRegistryClassId, NoRegistryValue)

class RegistrationValue : public NoRegistryValue {
public:
    static HRESULT WINAPI UpdateRegistry(BOOL) noexcept {
        // A custom callback receives the module scope without opening an
        // RGS root. Class-only registration must not acquire unused rights.
        if (!OpenATL::COM::rgs_resource_detail::CurrentModule() ||
            OpenATL::COM::rgs_resource_detail::Current()) return E_UNEXPECTED;
        ++registrationCalls;
        return S_OK;
    }
};
OBJECT_ENTRY_AUTO(registrationClassId, RegistrationValue)

class ExeRegistryValue : public NoRegistryValue {
public:
    static HRESULT WINAPI UpdateRegistry(BOOL install) noexcept {
        if (!ATL::_pAtlModule || !OpenATL::COM::rgs_resource_detail::CurrentModule())
            return E_UNEXPECTED;
        ++exeRegistryCalls;
        ATL::_ATL_REGMAP_ENTRY entries[] = {
            {L"KEY", L"OpenATL.Exe.AutoRegistry"}, {L"FIELD", L"Description"},
            {L"VALUE", L"EXE object map"}, {nullptr, nullptr}
        };
        return ATL::_pAtlModule->UpdateRegistryFromResourceS(L"OPENATL_PARAMETERS", install, entries);
    }
    HRESULT STDMETHODCALLTYPE GetClassID(CLSID* output) override {
        if (!output) return E_POINTER;
        *output = exeRegistryClassId;
        return S_OK;
    }
};
OBJECT_ENTRY_AUTO(exeRegistryClassId, ExeRegistryValue)

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
        CHECK(module.SetClassObjects(nullptr, 0) == S_OK); // Explicitly disable the automatic map.
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
        CHECK(!module.ParseCommandLine(L"app.exe /RegServer", &result) && FAILED(result));
        bool previous = false, enabled = false;
        CHECK(ATL::AtlGetPerUserRegistration(&previous) == S_OK);
        CHECK(!module.ParseCommandLine(L"app.exe /RegServerPerUser", &result) && FAILED(result));
        CHECK(ATL::AtlGetPerUserRegistration(&enabled) == S_OK && enabled);
        CHECK(ATL::AtlSetPerUserRegistration(previous) == S_OK);
        CHECK(module.SetClassObjects(nullptr, 0) == S_OK);
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
        CHECK(module.RegisterServer(FALSE, &exeRegistryClassId) == RPC_E_CHANGED_MODE &&
              module.UnregisterServer(FALSE, &exeRegistryClassId) == RPC_E_CHANGED_MODE);
        CHECK(module.RegisterAppId() == RPC_E_CHANGED_MODE && module.UnregisterAppId() == RPC_E_CHANGED_MODE);
        CHECK(module.RegisterClassObjects(CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE) == RPC_E_CHANGED_MODE);
        bool before = false, after = false;
        HRESULT parsed = S_OK;
        CHECK(ATL::AtlGetPerUserRegistration(&before) == S_OK);
        CHECK(!module.ParseCommandLine(L"app.exe /RegServerPerUser", &parsed) && parsed == RPC_E_CHANGED_MODE);
        CHECK(ATL::AtlGetPerUserRegistration(&after) == S_OK && before == after);
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

class RegistrationModule : public ATL::CAtlModuleT<RegistrationModule> {};
bool executable_registration() {
    CHECK(noCom() && !ATL::_pAtlModule && exeRegistryCalls == 0);
    ModuleRegistryRoot root;
    CHECK(root.Open());
    const wchar_t* name = L"OpenATL.Exe.AutoRegistry";
    const auto exists = [&]() {
        ATL::CRegKey key;
        return key.Open(root.key, name, KEY_READ) == ERROR_SUCCESS;
    };
    {
        Plain module;
        CHECK(module.GetInitHRESULT() == S_OK);
        OpenATL::COM::ScopedRgsRegistrationContext context(
            root.key, ::GetModuleHandleW(nullptr), L"ExeAutoRegistry");
        CHECK(context.Status() == S_OK);
        CHECK(module.RegisterServer(FALSE) == E_NOTIMPL && exeRegistryCalls == 0 && !exists());
        CHECK(module.RegisterServer(FALSE, &CLSID_NULL) == CLASS_E_CLASSNOTAVAILABLE && !exists());
        CHECK(FAILED(module.RegisterServer(TRUE, &exeRegistryClassId)) && exeRegistryCalls == 0 && !exists());
        CHECK(FAILED(module.UnregisterServer(TRUE, &exeRegistryClassId)) && exeRegistryCalls == 0);
        HRESULT other = S_OK, removed = S_OK;
        std::thread wrong([&] {
            other = module.RegisterServer(FALSE, &exeRegistryClassId);
            removed = module.UnregisterServer(FALSE, &exeRegistryClassId);
        });
        wrong.join();
        CHECK(other == RPC_E_WRONG_THREAD && removed == RPC_E_WRONG_THREAD && exeRegistryCalls == 0);
        CHECK(module.RegisterServer(FALSE, &exeRegistryClassId) == S_OK && exists());
        ATL::CRegKey key;
        CHECK(key.Open(root.key, name, KEY_READ | KEY_WRITE) == ERROR_SUCCESS);
        const std::wstring path = OpenATL::COM::rgs_resource_detail::Current()->modulePath;
        const auto valueIs = [&](LPCWSTR field, const std::wstring& expected) {
            wchar_t actual[OpenATL::RGS::MaxPathUnits + 32]{};
            ULONG count = static_cast<ULONG>(sizeof(actual) / sizeof(actual[0]));
            return key.QueryStringValue(field, actual, &count) == ERROR_SUCCESS && expected == actual;
        };
        CHECK(valueIs(L"Module", L"\"" + path + L"\"") && valueIs(L"RawModule", path) &&
              valueIs(L"OpenATL.COMOwner", L"RGS1:ExeAutoRegistry:" + path));
        CHECK(key.SetStringValue(L"Description", L"external modification") == ERROR_SUCCESS);
        key.Close();
        CHECK(FAILED(module.UnregisterServer(FALSE, &exeRegistryClassId)) && exists());
        CHECK(key.Open(root.key, name, KEY_WRITE) == ERROR_SUCCESS &&
              key.SetStringValue(L"Description", L"prefix EXE object map suffix") == ERROR_SUCCESS);
        key.Close();
        CHECK(module.UnregisterServer(FALSE, &exeRegistryClassId) == S_OK && !exists());
        CHECK(exeRegistryCalls == 3 && module.GetLockCount() == 0);
        module.Term();
        CHECK(module.RegisterServer(FALSE, &exeRegistryClassId) == E_UNEXPECTED &&
              module.UnregisterServer(FALSE, &exeRegistryClassId) == E_UNEXPECTED);
    }
    CHECK(noCom() && !ATL::_pAtlModule &&
          !OpenATL::COM::rgs_resource_detail::Current() &&
          !OpenATL::COM::rgs_resource_detail::CurrentModule());
    {
        // The common module itself performs the same registration without
        // opting into the EXE wrapper's apartment initialization.
        RegistrationModule module;
        OpenATL::COM::ScopedRgsRegistrationContext context(
            root.key, ::GetModuleHandleW(nullptr), L"BaseAutoRegistry");
        CHECK(context.Status() == S_OK && noCom());
        CHECK(module.RegisterServer(FALSE, &exeRegistryClassId) == S_OK && exists() && noCom());
        CHECK(module.UnregisterServer(FALSE, &exeRegistryClassId) == S_OK && !exists() && noCom());
    }
    CHECK(exeRegistryCalls == 5 && noCom() && !ATL::_pAtlModule);
    return true;
}

constexpr LPCWSTR appIdText = L"{DF014EC0-2477-450C-B47C-C493B26D9B90}";
constexpr LPCWSTR appIdPath = L"AppID\\{DF014EC0-2477-450C-B47C-C493B26D9B90}";
constexpr LPCWSTR appAlias = L"AppID\\OpenWTL.AppId.Test.exe";
bool registryKeyExists(HKEY root, LPCWSTR path) {
    ATL::CRegKey key;
    return key.Open(root, path, KEY_READ) == ERROR_SUCCESS;
}
template<class T, template<class> class Module>
class AppIdFixture : public Module<T> {
public:
    DECLARE_REGISTRY_APPID_RESOURCEID(IDR_OPENATL_APPID, "{DF014EC0-2477-450C-B47C-C493B26D9B90}")
    HKEY root = nullptr;
    HRESULT commonResult = S_OK, installResult = S_OK, removeResult = S_OK;
    bool throwCommon = false, flipMode = false, failRollback = false;
    int installs = 0, removals = 0;
    HRESULT AddCommonRGSReplacements(IRegistrarBase* registrar) override {
        const auto* scope = OpenATL::COM::rgs_resource_detail::CurrentModule();
        if (scope && !scope->PerUserRegistration()) return E_UNEXPECTED;
        if (throwCommon) throw std::bad_alloc();
        if (FAILED(commonResult)) return commonResult;
        if (flipMode) ATL::AtlSetPerUserRegistration(false);
        return registrar->AddReplacement(L"APP_VALUE", L"OpenWTL AppID");
    }
    HRESULT RegisterServer(BOOL, const CLSID* = nullptr) noexcept {
        ++installs;
        const auto* scope = OpenATL::COM::rgs_resource_detail::CurrentModule();
        if (!scope || !scope->PerUserRegistration() || !registryKeyExists(root, appIdPath))
            return E_UNEXPECTED;
        if (failRollback) commonResult = E_ABORT;
        return installResult; // Ordering probe, not a TypeLib registration substitute.
    }
    HRESULT UnregisterServer(BOOL, const CLSID* = nullptr) noexcept {
        ++removals;
        const auto* scope = OpenATL::COM::rgs_resource_detail::CurrentModule();
        if (!scope || !scope->PerUserRegistration() || !registryKeyExists(root, appIdPath))
            return E_UNEXPECTED;
        return removeResult;
    }
};
class AppIdBase : public AppIdFixture<AppIdBase, ATL::CAtlModuleT> {};
class AppIdExe : public AppIdFixture<AppIdExe, ATL::CAtlExeModuleT> {};
class AppIdDll : public AppIdFixture<AppIdDll, ATL::CAtlDllModuleT> {};
class InvalidAppId : public ATL::CAtlModuleT<InvalidAppId> {
public:
    DECLARE_REGISTRY_APPID_RESOURCEID(IDR_OPENATL_APPID, "invalid")
};
class MissingAppIdResource : public ATL::CAtlModuleT<MissingAppIdResource> {
public:
    DECLARE_REGISTRY_APPID_RESOURCEID(65535, "{DF014EC0-2477-450C-B47C-C493B26D9B90}")
};

template<class Module>
bool appid_registration(bool perUserCommand = false) {
    CHECK(noCom() && !ATL::_pAtlModule);
    struct RestoreMode {
        bool previous = false;
        bool armed = false;
        ~RestoreMode() { if (armed) ATL::AtlSetPerUserRegistration(previous); }
    } mode;
    CHECK(ATL::AtlGetPerUserRegistration(&mode.previous) == S_OK &&
          ATL::AtlSetPerUserRegistration(true) == S_OK);
    mode.armed = true;
    ModuleRegistryRoot root;
    CHECK(root.Open());
    ATL::CRegKey shared;
    CHECK(shared.Create(root.key, L"AppID") == ERROR_SUCCESS &&
          shared.SetStringValue(L"Foreign", L"preserved") == ERROR_SUCCESS);
    shared.Close();
    {
        Module module;
        module.root = root.key;
        OpenATL::COM::ScopedRgsRegistrationContext context(
            root.key, ::GetModuleHandleW(nullptr), L"AppIdNativeTest");
        CHECK(context.Status() == S_OK && std::wstring_view(Module::GetAppId()) == appIdText);
        CHECK(module.RegisterAppId() == S_OK && registryKeyExists(root.key, appAlias));
        ATL::CRegKey key;
        CHECK(key.Open(root.key, appIdPath, KEY_READ | KEY_WRITE) == ERROR_SUCCESS);
        wchar_t value[OpenATL::RGS::MaxPathUnits + 64]{};
        ULONG count = static_cast<ULONG>(sizeof(value) / sizeof(value[0]));
        const auto& path = OpenATL::COM::rgs_resource_detail::Current()->modulePath;
        CHECK(key.QueryStringValue(L"Module", value, &count) == ERROR_SUCCESS && path == value);
        count = static_cast<ULONG>(sizeof(value) / sizeof(value[0]));
        CHECK(key.QueryStringValue(L"OpenATL.COMOwner", value, &count) == ERROR_SUCCESS &&
              std::wstring(value) == L"RGS1:AppIdNativeTest:" + path);
        CHECK(key.SetStringValue(nullptr, L"foreign edit") == ERROR_SUCCESS);
        key.Close();
        CHECK(FAILED(module.UnregisterAppId()) && registryKeyExists(root.key, appIdPath) &&
              registryKeyExists(root.key, appAlias));
        CHECK(key.Open(root.key, appIdPath, KEY_WRITE) == ERROR_SUCCESS &&
              key.SetStringValue(nullptr, L"OpenWTL AppID") == ERROR_SUCCESS);
        key.Close();
        CHECK(module.UnregisterAppId() == S_OK && !registryKeyExists(root.key, appIdPath) &&
              !registryKeyExists(root.key, appAlias));
        module.commonResult = E_ACCESSDENIED;
        CHECK(module.RegisterAppId() == E_ACCESSDENIED && module.installs == 0);
        module.commonResult = S_OK;
        module.throwCommon = true;
        CHECK(module.RegisterAppId() == E_OUTOFMEMORY && !registryKeyExists(root.key, appIdPath));
        module.throwCommon = false;
        if constexpr (std::is_same<Module, AppIdExe>::value) {
            HRESULT install = S_OK, remove = S_OK;
            std::thread wrong([&] { install = module.RegisterAppId(); remove = module.UnregisterAppId(); });
            wrong.join();
            CHECK(install == RPC_E_WRONG_THREAD && remove == RPC_E_WRONG_THREAD);
        }
        const auto install = [&]() {
            if constexpr (std::is_same<Module, AppIdExe>::value) {
                HRESULT hr = E_FAIL;
                if (perUserCommand) ATL::AtlSetPerUserRegistration(false);
                return !module.ParseCommandLine(perUserCommand ? L"test.exe /rEgSeRvErPeRuSeR" :
                    L"test.exe /RegServer", &hr) ? hr : E_UNEXPECTED;
            } else if constexpr (std::is_same<Module, AppIdDll>::value) return module.DllRegisterServer(FALSE);
            else return module.RegisterAppId();
        };
        const auto remove = [&]() {
            if constexpr (std::is_same<Module, AppIdExe>::value) {
                HRESULT hr = E_FAIL;
                if (perUserCommand) ATL::AtlSetPerUserRegistration(false);
                return !module.ParseCommandLine(perUserCommand ? L"test.exe -uNrEgSeRvErPeRuSeR" :
                    L"test.exe -UnregServer", &hr) ? hr : E_UNEXPECTED;
            } else if constexpr (std::is_same<Module, AppIdDll>::value) return module.DllUnregisterServer(FALSE);
            else return module.UnregisterAppId();
        };
        if constexpr (!std::is_same<Module, AppIdBase>::value) {
            module.flipMode = true;
            CHECK(install() == S_OK && module.installs == 1 && registryKeyExists(root.key, appIdPath));
            CHECK(ATL::AtlSetPerUserRegistration(true) == S_OK);
            CHECK(remove() == S_OK && module.removals == 1 && !registryKeyExists(root.key, appIdPath));
            module.flipMode = false;
            CHECK(ATL::AtlSetPerUserRegistration(true) == S_OK);
            module.installResult = E_ACCESSDENIED;
            CHECK(install() == E_ACCESSDENIED && !registryKeyExists(root.key, appIdPath));
            module.failRollback = true;
            CHECK(install() == E_ABORT && registryKeyExists(root.key, appIdPath));
            module.commonResult = S_OK;
            module.failRollback = false;
            CHECK(module.UnregisterAppId() == S_OK);
            module.installResult = S_OK;
            CHECK(install() == S_OK);
            module.removeResult = E_ACCESSDENIED;
            CHECK(remove() == E_ACCESSDENIED && registryKeyExists(root.key, appIdPath));
            module.removeResult = S_OK;
            CHECK(remove() == S_OK && !registryKeyExists(root.key, appIdPath));
        }
        CHECK(key.Open(root.key, L"AppID", KEY_READ) == ERROR_SUCCESS);
        count = static_cast<ULONG>(sizeof(value) / sizeof(value[0]));
        CHECK(key.QueryStringValue(L"Foreign", value, &count) == ERROR_SUCCESS &&
              std::wstring_view(value) == L"preserved");
        key.Close();
        module.Term();
        CHECK(module.RegisterAppId() == E_UNEXPECTED && module.UnregisterAppId() == E_UNEXPECTED &&
              Module::UpdateRegistryAppId(TRUE) == E_UNEXPECTED);
    }
    CHECK(noCom() && !ATL::_pAtlModule && !OpenATL::COM::rgs_resource_detail::Current() &&
          !OpenATL::COM::rgs_resource_detail::CurrentModule());
    return true;
}

class PerUserCommandModule : public ATL::CAtlExeModuleT<PerUserCommandModule> {
public:
    std::wstring key;
    HRESULT outcome = S_OK;
    bool throwInstall = false;
    int installs = 0, removals = 0;
    static constexpr LPCWSTR script = L"HKCR { ForceRemove '%COMMAND_KEY%' = s '%MODULE%' "
        L"{ val RawModule = s '%MODULE_RAW%' } }";
    HRESULT RegisterServer(BOOL library) {
        ++installs;
        if (throwInstall) throw ATL::CAtlException(E_ABORT);
        return Apply(library, true);
    }
    HRESULT UnregisterServer(BOOL library) {
        ++removals;
        return Apply(library, false);
    }
private:
    HRESULT Apply(BOOL library, bool install) {
        const auto* scope = OpenATL::COM::rgs_resource_detail::CurrentModule();
        if (!library || !scope || !scope->PerUserRegistration()) return E_UNEXPECTED;
        if (FAILED(outcome)) return outcome;
        // Real default-root RGS execution; the TRUE argument is a command
        // dispatch probe, not a claim of EXE embedded-TypeLib qualification.
        ATL::CRegObject registrar;
        const HRESULT hr = registrar.AddReplacement(L"COMMAND_KEY", key.c_str());
        if (FAILED(hr)) return hr;
        return install ? registrar.StringRegister(script) : registrar.StringUnregister(script);
    }
};

bool per_user_commands() {
    CHECK(noCom() && !ATL::_pAtlModule);
    struct RestoreMode {
        bool previous = false;
        bool armed = false;
        ~RestoreMode() { if (armed) ATL::AtlSetPerUserRegistration(previous); }
    } mode;
    CHECK(ATL::AtlGetPerUserRegistration(&mode.previous) == S_OK &&
          ATL::AtlSetPerUserRegistration(false) == S_OK);
    mode.armed = true;
    const auto enabled = []() {
        bool value = false;
        return ATL::AtlGetPerUserRegistration(&value) == S_OK && value;
    };
    {
        PerUserCommandModule module;
        GUID id{};
        wchar_t token[40]{};
        CHECK(::CoCreateGuid(&id) == S_OK && ::StringFromGUID2(id, token, 40));
        module.key = L"OpenWTL.Command.";
        for (wchar_t ch : token) if (OpenATL::RGS::detail::Hex(ch)) module.key += ch;
        const std::wstring path = L"Software\\Classes\\" + module.key;
        CHECK(!registryKeyExists(HKEY_CURRENT_USER, path.c_str()));
        struct Cleanup {
            PerUserCommandModule& module;
            bool installed = false;
            ~Cleanup() {
                if (!installed) return;
                module.outcome = S_OK;
                HRESULT hr = E_FAIL;
                module.ParseCommandLine(L"test.exe /UnregServerPerUser", &hr);
                if (FAILED(hr)) std::fprintf(stderr, "ATL_COMMAND_CLEANUP_FAIL\n");
            }
        } cleanup{module};
        HRESULT result = E_FAIL;
        CHECK(module.ParseCommandLine(L"\"C:\\RegServerPerUser path\\app.exe\" /Embedding", &result) &&
              result == S_OK && !enabled());
        CHECK(module.ParseCommandLine(L"test.exe /RegServerPerUserSuffix", &result) && result == S_OK && !enabled());
        CHECK(!module.ParseCommandLine(nullptr, &result) && result == E_INVALIDARG && !enabled());
        CHECK(!module.ParseCommandLine(L"test.exe /RegServerPerUser", nullptr) && !enabled());
        const std::wstring oversized(32767, L'x');
        CHECK(!module.ParseCommandLine(oversized.c_str(), &result) &&
              result == HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW) && !enabled());
        bool wrongRun = true;
        std::thread wrong([&] { wrongRun = module.ParseCommandLine(L"test.exe /RegServerPerUser", &result); });
        wrong.join();
        CHECK(!wrongRun && result == RPC_E_WRONG_THREAD && !enabled());
        {
            OpenATL::COM::ScopedModuleRgsRegistration outer(::GetModuleHandleW(nullptr), false);
            CHECK(!module.ParseCommandLine(L"test.exe /RegServerPerUser", &result) &&
                  result == E_UNEXPECTED && !enabled());
        }
        CHECK(module.installs == 0 && module.removals == 0);
        module.outcome = E_ACCESSDENIED;
        CHECK(!module.ParseCommandLine(L"test.exe /rEgSeRvErPeRuSeR", &result) &&
              result == E_ACCESSDENIED && enabled());
        CHECK(ATL::AtlSetPerUserRegistration(false) == S_OK);
        module.throwInstall = true;
        CHECK(!module.ParseCommandLine(L"test.exe -RegServerPerUser", &result) && result == E_ABORT && enabled());
        module.throwInstall = false;
        module.outcome = S_OK;
        CHECK(ATL::AtlSetPerUserRegistration(false) == S_OK);
        cleanup.installed = true;
        CHECK(!module.ParseCommandLine(L"test.exe /RegServerPerUser", &result) && result == S_OK && enabled());
        ATL::CRegKey key;
        CHECK(key.Open(HKEY_CURRENT_USER, path.c_str(), KEY_READ) == ERROR_SUCCESS);
        wchar_t image[OpenATL::RGS::MaxPathUnits + 2]{}, value[OpenATL::RGS::MaxPathUnits + 32]{};
        CHECK(::GetModuleFileNameW(nullptr, image, static_cast<DWORD>(sizeof(image) / sizeof(image[0]))));
        ULONG count = static_cast<ULONG>(sizeof(value) / sizeof(value[0]));
        CHECK(key.QueryStringValue(nullptr, value, &count) == ERROR_SUCCESS &&
              std::wstring(value) == std::wstring(L"\"") + image + L"\"");
        count = static_cast<ULONG>(sizeof(value) / sizeof(value[0]));
        CHECK(key.QueryStringValue(L"RawModule", value, &count) == ERROR_SUCCESS && std::wstring_view(value) == image);
        key.Close();
        CHECK(ATL::AtlSetPerUserRegistration(false) == S_OK);
        module.outcome = E_ACCESSDENIED;
        CHECK(!module.ParseCommandLine(L"test.exe -UnregServerPerUser", &result) &&
              result == E_ACCESSDENIED && enabled() && registryKeyExists(HKEY_CURRENT_USER, path.c_str()));
        module.outcome = S_OK;
        CHECK(!module.ParseCommandLine(L"test.exe -uNrEgSeRvErPeRuSeR", &result) && result == S_OK && enabled());
        cleanup.installed = false;
        CHECK(!registryKeyExists(HKEY_CURRENT_USER, path.c_str()));
        // Ordinary commands preserve the policy selected by a previous command.
        cleanup.installed = true;
        CHECK(!module.ParseCommandLine(L"test.exe /RegServer", &result) && result == S_OK && enabled());
        CHECK(!module.ParseCommandLine(L"test.exe /UnregServer", &result) && result == S_OK && enabled());
        cleanup.installed = false;
        CHECK(!registryKeyExists(HKEY_CURRENT_USER, path.c_str()));
        CHECK(ATL::AtlSetPerUserRegistration(false) == S_OK);
        module.Term();
        CHECK(!module.ParseCommandLine(L"test.exe /RegServerPerUser", &result) && result == E_UNEXPECTED && !enabled());
    }
    CHECK(noCom() && !ATL::_pAtlModule && !OpenATL::COM::rgs_resource_detail::Current() &&
          !OpenATL::COM::rgs_resource_detail::CurrentModule());
    return true;
}

bool appid_guards() {
    CHECK(noCom() && !ATL::_pAtlModule);
    { RegistrationModule module;
      CHECK(!RegistrationModule::GetAppId() && module.RegisterAppId() == S_FALSE &&
            module.UnregisterAppId() == S_FALSE && noCom()); }
    { InvalidAppId module; CHECK(module.RegisterAppId() == E_INVALIDARG); }
    { MissingAppIdResource module; CHECK(FAILED(module.RegisterAppId())); }
    CHECK(noCom() && !ATL::_pAtlModule && !OpenATL::COM::rgs_resource_detail::Current() &&
          !OpenATL::COM::rgs_resource_detail::CurrentModule());
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
        CHECK(FAILED(dll.DllRegisterServer(TRUE)));
        CHECK(FAILED(dll.DllUnregisterServer(TRUE)));

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
        CHECK(dll.RegisterServer(FALSE, &autoClassId) == E_NOTIMPL);
        CHECK(dll.UnregisterServer(FALSE, &autoClassId) == E_NOTIMPL);
        CHECK(dll.RegisterServer(FALSE, nullptr) == E_NOTIMPL);
        CHECK(dll.RegisterServer(FALSE, &CLSID_NULL) == CLASS_E_CLASSNOTAVAILABLE);
        CHECK(dll.RegisterServer(FALSE, &noRegistryClassId) == S_OK);
        CHECK(dll.UnregisterServer(FALSE, &noRegistryClassId) == S_OK);
        // TRUE used to be ignored, executing the class callback and claiming
        // a complete install. Rejection must precede every class side effect.
        CHECK(registrationCalls == 0);
        // This EXE has no embedded TypeLib: native loading must fail before
        // callbacks, rather than ignore TRUE or invent a registration.
        CHECK(FAILED(dll.RegisterServer(TRUE, &registrationClassId)));
        CHECK(FAILED(dll.UnregisterServer(TRUE, &registrationClassId)));
        CHECK(FAILED(dll.DllRegisterServer()));
        CHECK(FAILED(dll.DllUnregisterServer()));
        CHECK(registrationCalls == 0 && dll.GetLockCount() == 0);
        CHECK(dll.RegisterServer(FALSE, &registrationClassId) == S_OK);
        CHECK(dll.UnregisterServer(FALSE, &registrationClassId) == S_OK);
        CHECK(registrationCalls == 2);
        // This EXE has no resource named Missing.RGS. Static-helper failures
        // must finish before acquiring a default root and release their scope.
        CHECK(FAILED(dll.UpdateRegistryFromResourceS(L"Missing.RGS", TRUE)));
        CHECK(!OpenATL::COM::rgs_resource_detail::Current() &&
              !OpenATL::COM::rgs_resource_detail::CurrentModule());
        CHECK(noCom() && dll.DllCanUnloadNow() == S_OK);
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

bool activateAutomaticValue() {
    ATL::CComPtr<IClassFactory> factory;
    CHECK(::CoGetClassObject(autoClassId, CLSCTX_LOCAL_SERVER, nullptr, IID_IClassFactory,
                            reinterpret_cast<void**>(&factory.p)) == S_OK && factory.p);
    void* output = reinterpret_cast<void*>(1);
    CHECK(factory->CreateInstance(reinterpret_cast<IUnknown*>(1), IID_IPersist, &output) ==
          CLASS_E_NOAGGREGATION && !output);
    ATL::CComPtr<IPersist> first, second;
    CHECK(factory->CreateInstance(nullptr, IID_IPersist, reinterpret_cast<void**>(&first.p)) == S_OK);
    CHECK(::CoCreateInstance(autoClassId, nullptr, CLSCTX_LOCAL_SERVER, IID_IPersist,
                             reinterpret_cast<void**>(&second.p)) == S_OK);
    CHECK(autoLive == 2 && ATL::_pAtlModule->GetLockCount() == 2);
    CLSID actual{};
    CHECK(first->GetClassID(&actual) == S_OK && IsEqualCLSID(actual, autoClassId));
    CHECK(second->GetClassID(&actual) == S_OK && IsEqualCLSID(actual, autoClassId));
    CHECK(factory->LockServer(FALSE) == E_UNEXPECTED && factory->LockServer(TRUE) == S_OK &&
          factory->LockServer(FALSE) == S_OK);
    first.Release();
    second.Release();
    factory.Release();
    CHECK(autoLive == 0 && ATL::_pAtlModule->GetLockCount() == 0);
    return true;
}

template<bool Apartment>
class AutomaticExecutable : public ATL::CAtlExeModuleT<AutomaticExecutable<Apartment>> {
public:
    static HRESULT InitializeCom() noexcept {
        return ::CoInitializeEx(nullptr, Apartment ? COINIT_APARTMENTTHREADED : COINIT_MULTITHREADED);
    }
    int loops = 0;
    bool activated = false;
    bool throwInLoop = false;
    bool CanRetire() const noexcept {
        const auto* server = this->autoMap_.Server();
        return !server || server->CanUnloadNow() == S_OK;
    }
    void RunMessageLoop() {
        ++loops;
        activated = activateAutomaticValue() &&
            this->SetClassObjects(nullptr, 0) == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
        if (throwInLoop) throw ATL::CAtlException(E_ABORT);
        ::PostQuitMessage(0);
        ATL::CAtlExeModuleT<AutomaticExecutable<Apartment>>::RunMessageLoop();
    }
};

bool absentAutomaticFactory() {
    void* output = reinterpret_cast<void*>(1);
    CHECK(::CoGetClassObject(autoClassId, CLSCTX_LOCAL_SERVER, nullptr, IID_IClassFactory,
                            &output) == REGDB_E_CLASSNOTREG && !output);
    return true;
}

template<bool Apartment>
bool automatic_executable() {
    CHECK(noCom() && !ATL::_pAtlModule && autoLive == 0);
    const int releasedBefore = autoFinalRelease.load();
    const int registryBefore = exeRegistryCalls;
    {
        AutomaticExecutable<Apartment> module;
        CHECK(module.GetInitHRESULT() == S_OK && absentAutomaticFactory());
        // An invalid explicit override must not disable the automatic map.
        CHECK(module.SetClassObjects(nullptr, 1) == E_INVALIDARG);
        CHECK(module.RegisterClassObjects(CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE) == S_OK);
        CHECK(!module.CanRetire()); // The OS owns the automatic factories.
        CHECK(module.RegisterClassObjects(CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE) ==
              HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS));
        CHECK(module.SetClassObjects(nullptr, 0) == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS));
        HRESULT registered = S_OK, revoked = S_OK;
        std::thread wrong([&] {
            registered = module.RegisterClassObjects(CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE);
            revoked = module.RevokeClassObjects();
        });
        wrong.join();
        CHECK(registered == RPC_E_WRONG_THREAD && revoked == RPC_E_WRONG_THREAD);
        CHECK(activateAutomaticValue());
        ATL::CComPtr<IClassFactory> retained;
        CHECK(::CoGetClassObject(autoClassId, CLSCTX_LOCAL_SERVER, nullptr, IID_IClassFactory,
                                reinterpret_cast<void**>(&retained.p)) == S_OK);
        CHECK(module.RevokeClassObjects() == S_OK && module.RevokeClassObjects() == S_FALSE);
        CHECK(absentAutomaticFactory() && !module.CanRetire());
        // Revocation retires OS publication; the client's retained factory
        // still owns this engine and can finish work before module teardown.
        ATL::CComPtr<IPersist> value;
        CHECK(retained->CreateInstance(nullptr, IID_IPersist, reinterpret_cast<void**>(&value.p)) == S_OK);
        CHECK(retained->LockServer(TRUE) == S_OK && retained->LockServer(FALSE) == S_OK);
        value.Release();
        retained.Release();
        CHECK(module.CanRetire() && module.GetLockCount() == 0);
        CHECK(module.Run() == S_OK && module.loops == 1 && module.activated && module.CanRetire());
        CHECK(absentAutomaticFactory());
        module.throwInLoop = true;
        CHECK(module.Run() == E_ABORT && module.loops == 2 && module.activated && module.CanRetire());
        CHECK(module.RevokeClassObjects() == S_FALSE && absentAutomaticFactory());
        CHECK(module.SetClassObjects(nullptr, 0) == S_OK && module.Run() == S_FALSE && module.loops == 2);
    }
    CHECK(noCom() && !ATL::_pAtlModule && autoLive == 0 &&
          autoFinalRelease == releasedBefore + 7 && exeRegistryCalls == registryBefore);
    {
        AutomaticExecutable<Apartment> module;
        CHECK(absentAutomaticFactory());
        CHECK(module.RegisterClassObjects(CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE) == S_OK);
        module.Term(); // Explicit retirement revokes publication before Term callbacks.
        CHECK(module.CanRetire() && absentAutomaticFactory());
        CHECK(module.RegisterClassObjects(CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE) == E_UNEXPECTED);
    }
    CHECK(noCom() && !ATL::_pAtlModule);
    {
        AutomaticExecutable<Apartment> module;
        CHECK(absentAutomaticFactory());
        CHECK(module.RegisterClassObjects(CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE) == S_OK);
        // Leave the registrations to the destructor; its Term runs while
        // the owning apartment is still initialized.
    }
    CHECK(noCom() && !ATL::_pAtlModule);
    {
        AutomaticExecutable<Apartment> module;
        CHECK(absentAutomaticFactory() && module.CanRetire());
    }
    CHECK(noCom() && !ATL::_pAtlModule);
    return true;
}

// This fixture uses the system IDispatch/IPersist proxy-stubs. A fresh CLSID
// and named-event token belong only to the one SCM-launched server process.
CLSID processClassId{};
class ProcessValue : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>,
                     public IPersist, public IDispatch {
public:
    BEGIN_COM_MAP(ProcessValue)
        COM_INTERFACE_ENTRY(IPersist)
        COM_INTERFACE_ENTRY(IDispatch)
    END_COM_MAP()
    HRESULT STDMETHODCALLTYPE GetClassID(CLSID* output) override {
        if (!output) return E_POINTER;
        *output = processClassId;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* count) override {
        if (!count) return E_POINTER;
        *count = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo** output) override {
        if (!output) return E_POINTER;
        *output = nullptr;
        return DISP_E_BADINDEX;
    }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID iid, LPOLESTR* names, UINT count,
                                            LCID, DISPID* output) override {
        if (!::IsEqualIID(iid, IID_NULL)) return DISP_E_UNKNOWNINTERFACE;
        if (!names || !output || count != 1 || !names[0]) return E_INVALIDARG;
        *output = DISPID_UNKNOWN;
        if (::_wcsicmp(names[0], L"ProcessId")) return DISP_E_UNKNOWNNAME;
        *output = 1;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID member, REFIID iid, LCID, WORD flags,
        DISPPARAMS* parameters, VARIANT* output, EXCEPINFO*, UINT*) override {
        if (!::IsEqualIID(iid, IID_NULL)) return DISP_E_UNKNOWNINTERFACE;
        if (member != 1 || flags != DISPATCH_PROPERTYGET) return DISP_E_MEMBERNOTFOUND;
        if (!parameters || !output) return E_POINTER;
        if (parameters->cArgs || parameters->cNamedArgs) return DISP_E_BADPARAMCOUNT;
        ::VariantInit(output);
        V_VT(output) = VT_UI4;
        V_UI4(output) = ::GetCurrentProcessId();
        return S_OK;
    }
};

struct ProcessHandle {
    HANDLE value = nullptr;
    ProcessHandle() = default;
    ProcessHandle(const ProcessHandle&) = delete;
    ProcessHandle& operator=(const ProcessHandle&) = delete;
    ~ProcessHandle() { if (value) ::CloseHandle(value); }
};
struct ProcessEvents {
    ProcessHandle ready, stop, revoked;
    bool Open(const std::wstring& token, bool create) {
        const auto open = [&](ProcessHandle& target, LPCWSTR suffix) {
            const std::wstring name = L"Local\\OpenWTL.Exe." + token + suffix;
            if (create) {
                target.value = ::CreateEventW(nullptr, TRUE, FALSE, name.c_str());
                return target.value && ::GetLastError() != ERROR_ALREADY_EXISTS;
            }
            target.value = ::OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, name.c_str());
            return target.value != nullptr;
        };
        return open(ready, L".ready") && open(stop, L".stop") && open(revoked, L".revoked");
    }
};

template<bool Apartment>
class ProcessExecutable : public ATL::CAtlExeModuleT<ProcessExecutable<Apartment>> {
public:
    static HRESULT InitializeCom() noexcept {
        return ::CoInitializeEx(nullptr, Apartment ? COINIT_APARTMENTTHREADED : COINIT_MULTITHREADED);
    }
    ProcessEvents& events;
    bool completed = false;
    explicit ProcessExecutable(ProcessEvents& input) : events(input) {}
    bool CanRetire() const noexcept {
        const auto* server = this->autoMap_.Server();
        return this->GetLockCount() == 0 && (!server || server->CanUnloadNow() == S_OK);
    }
    HRESULT PreMessageLoop(int show) noexcept {
        const HRESULT hr = ATL::CAtlExeModuleT<ProcessExecutable<Apartment>>::PreMessageLoop(show);
        if (hr != S_OK) return hr;
        return ::SetEvent(events.ready.value) ? S_OK : HRESULT_FROM_WIN32(::GetLastError());
    }
    bool Pump(bool drain) {
        const ULONGLONG deadline = ::GetTickCount64() + 10000;
        for (;;) {
            if (drain && CanRetire()) return true;
            const DWORD wait = ::MsgWaitForMultipleObjects(drain ? 0 : 1,
                drain ? nullptr : &events.stop.value, FALSE, 20, QS_ALLINPUT);
            if (!drain && wait == WAIT_OBJECT_0) return true;
            if (wait == WAIT_FAILED) return false;
            MSG message{};
            while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (message.message == WM_QUIT) return false;
                ::TranslateMessage(&message);
                ::DispatchMessageW(&message);
            }
            if (::GetTickCount64() >= deadline) return false;
        }
    }
    void RunMessageLoop() {
        if (!Pump(false) || this->RevokeClassObjects() != S_OK ||
            !::SetEvent(events.revoked.value) || !Pump(true))
            throw ATL::CAtlException(HRESULT_FROM_WIN32(ERROR_TIMEOUT));
        completed = true;
    }
};

template<bool Apartment>
bool processServer(const std::wstring& token) {
    ProcessEvents events;
    CHECK(events.Open(token, false));
    // Construct the node only in the launched child, before its map freezes.
    static ATL::detail::AutoObjectEntry entry(processClassId,
        static_cast<OpenATL::COM::Creator>(&ATL::CComObject<ProcessValue>::CreateInstance));
    {
        ProcessExecutable<Apartment> module(events);
        CHECK(module.GetInitHRESULT() == S_OK);
        CHECK(module.Run() == S_FALSE && module.completed && module.CanRetire());
    }
    CHECK(noCom() && !ATL::_pAtlModule);
    return true;
}

const wchar_t* processScript =
    L"HKCR { NoRemove CLSID { ForceRemove '%SERVER_CLSID%' = s 'OpenWTL native EXE test' "
    L"{ LocalServer32 = s '%MODULE% %SERVER_MODE% %SERVER_CLSID%' "
    L"{ val ServerExecutable = s '%MODULE_RAW%' } } } }";

struct ProcessRegistration {
    ATL::CRegObject registrar;
    ProcessEvents events;
    ProcessHandle process;
    bool installed = false;
    bool Remove() {
        if (!installed) return true;
        const HRESULT hr = registrar.StringUnregister(processScript);
        if (SUCCEEDED(hr)) installed = false;
        return SUCCEEDED(hr);
    }
    ~ProcessRegistration() {
        if (!Remove()) std::fprintf(stderr, "ATL_EXE_PROCESS_CLEANUP_FAIL registry=1\n");
        if (events.stop.value) ::SetEvent(events.stop.value);
        if (process.value && ::WaitForSingleObject(process.value, 5000) != WAIT_OBJECT_0) {
            // This handle is accepted only after remote PID and image checks.
            // Forced termination is failure cleanup, never a passing admission.
            ::TerminateProcess(process.value, 1);
            ::WaitForSingleObject(process.value, 5000);
        }
    }
};

bool remoteProcessId(IDispatch* object, DWORD& pid) {
    LPOLESTR name = const_cast<LPOLESTR>(L"ProcessId");
    DISPID member = DISPID_UNKNOWN;
    CHECK(object->GetIDsOfNames(IID_NULL, &name, 1, LOCALE_USER_DEFAULT, &member) == S_OK && member == 1);
    DISPPARAMS parameters{};
    ATL::CComVariant result;
    CHECK(object->Invoke(member, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_PROPERTYGET,
        &parameters, &result, nullptr, nullptr) == S_OK && V_VT(&result) == VT_UI4);
    pid = V_UI4(&result);
    return true;
}

bool cross_process_executable(bool apartment) {
    CHECK(noCom() && !ATL::_pAtlModule);
    struct ComBalance { ~ComBalance() { ::CoUninitialize(); } };
    {
        CHECK(::CoInitializeEx(nullptr, COINIT_MULTITHREADED) == S_OK);
        ComBalance balance;
        struct RestoreMode {
            bool previous = false;
            bool armed = false;
            ~RestoreMode() { if (armed) ATL::AtlSetPerUserRegistration(previous); }
        } mode;
        CHECK(ATL::AtlGetPerUserRegistration(&mode.previous) == S_OK &&
              ATL::AtlSetPerUserRegistration(true) == S_OK);
        mode.armed = true;
        GUID id{};
        wchar_t guid[40]{}, image[OpenATL::RGS::MaxPathUnits + 2]{};
        CHECK(::CoCreateGuid(&id) == S_OK && ::StringFromGUID2(id, guid, 40));
        const DWORD length = ::GetModuleFileNameW(nullptr, image,
            static_cast<DWORD>(sizeof(image) / sizeof(image[0])));
        CHECK(length && length <= OpenATL::RGS::MaxPathUnits);
        const std::wstring classPath = std::wstring(L"Software\\Classes\\CLSID\\") + guid;
        const auto absent = [&]() {
            ATL::CRegKey key;
            return key.Open(HKEY_CURRENT_USER, classPath.c_str(), KEY_READ) == ERROR_FILE_NOT_FOUND;
        };
        CHECK(absent());
        ProcessRegistration session;
        CHECK(session.events.Open(guid, true));
        CHECK(session.registrar.AddReplacement(L"SERVER_CLSID", guid) == S_OK &&
              session.registrar.AddReplacement(L"SERVER_MODE",
                  apartment ? L"--module-server-sta" : L"--module-server-mta") == S_OK);
        CHECK(session.registrar.StringRegister(processScript) == S_OK);
        session.installed = true;
        {
            ATL::CRegKey key;
            CHECK(key.Open(HKEY_CURRENT_USER, (classPath + L"\\LocalServer32").c_str(), KEY_READ) == ERROR_SUCCESS);
            wchar_t command[OpenATL::RGS::MaxPathUnits + 128]{};
            ULONG count = static_cast<ULONG>(sizeof(command) / sizeof(command[0]));
            const std::wstring expected = std::wstring(L"\"") + image + L"\" " +
                (apartment ? L"--module-server-sta " : L"--module-server-mta ") + guid;
            CHECK(key.QueryStringValue(nullptr, command, &count) == ERROR_SUCCESS && expected == command);
            count = static_cast<ULONG>(sizeof(command) / sizeof(command[0]));
            CHECK(key.QueryStringValue(L"ServerExecutable", command, &count) == ERROR_SUCCESS &&
                  std::wstring_view(command) == image);
        }
        ATL::CComPtr<IClassFactory> factory;
        // No CreateProcess call: Windows SCM reads LocalServer32 and starts
        // this same executable with its -Embedding argument.
        CHECK(::CoGetClassObject(id, CLSCTX_LOCAL_SERVER, nullptr, IID_IClassFactory,
                                reinterpret_cast<void**>(&factory.p)) == S_OK);
        CHECK(::WaitForSingleObject(session.events.ready.value, 5000) == WAIT_OBJECT_0);
        ATL::CComPtr<IDispatch> object;
        CHECK(factory->CreateInstance(nullptr, IID_IDispatch, reinterpret_cast<void**>(&object.p)) == S_OK);
        DWORD pid = 0;
        CHECK(remoteProcessId(object, pid) && pid && pid != ::GetCurrentProcessId());
        HANDLE process = ::OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE,
                                       FALSE, pid);
        CHECK(process);
        wchar_t actualImage[OpenATL::RGS::MaxPathUnits + 2]{};
        DWORD capacity = static_cast<DWORD>(sizeof(actualImage) / sizeof(actualImage[0]));
        const bool sameImage = ::QueryFullProcessImageNameW(process, 0, actualImage, &capacity) &&
                               !::lstrcmpiW(image, actualImage);
        if (!sameImage) ::CloseHandle(process);
        CHECK(sameImage);
        session.process.value = process;
        ATL::CComPtr<IPersist> persist, second;
        CHECK(object->QueryInterface(IID_IPersist, reinterpret_cast<void**>(&persist.p)) == S_OK);
        CLSID actual{};
        CHECK(persist->GetClassID(&actual) == S_OK && ::IsEqualCLSID(actual, id));
        CHECK(::CoCreateInstance(id, nullptr, CLSCTX_LOCAL_SERVER, IID_IPersist,
                                reinterpret_cast<void**>(&second.p)) == S_OK);
        CHECK(second->GetClassID(&actual) == S_OK && ::IsEqualCLSID(actual, id));
        CHECK(factory->LockServer(TRUE) == S_OK && factory->LockServer(FALSE) == S_OK);
        CHECK(session.Remove() && absent());
        CHECK(::SetEvent(session.events.stop.value));
        CHECK(::WaitForSingleObject(session.events.revoked.value, 5000) == WAIT_OBJECT_0);
        DWORD after = 0;
        CHECK(remoteProcessId(object, after) && after == pid);
        CHECK(persist->GetClassID(&actual) == S_OK && ::IsEqualCLSID(actual, id));
        object.Release();
        persist.Release();
        second.Release();
        factory.Release();
        CHECK(::WaitForSingleObject(session.process.value, 5000) == WAIT_OBJECT_0);
        DWORD code = 1;
        CHECK(::GetExitCodeProcess(session.process.value, &code) && code == 0 && absent());
    }
    CHECK(noCom() && !ATL::_pAtlModule && !OpenATL::COM::rgs_resource_detail::Current() &&
          !OpenATL::COM::rgs_resource_detail::CurrentModule());
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
    {
        Plain module;
        CHECK(module.RegisterClassObjects(CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE) == E_INVALIDARG);
        CHECK(module.RevokeClassObjects() == S_FALSE && module.GetLockCount() == 0 &&
              absentAutomaticFactory());
    }
    CHECK(noCom() && !ATL::_pAtlModule && autoLive == 0);
    return true;
}
}

bool RunAtlModuleTests() {
    return module_locks() && executable() && executable_registration() &&
           appid_guards() && appid_registration<AppIdBase>() && appid_registration<AppIdExe>() &&
           appid_registration<AppIdExe>(true) &&
           appid_registration<AppIdDll>() && per_user_commands() && dll_module() &&
           auto_object_map() && automatic_executable<false>() && automatic_executable<true>() &&
           cross_process_executable(false) && cross_process_executable(true) &&
           duplicate_auto_map();
}

int RunAtlModuleServer(const char* input, bool apartment) {
    if (!input || std::strlen(input) != 38) return 2;
    wchar_t token[39]{};
    for (size_t i = 0; i < 38; ++i) token[i] = static_cast<unsigned char>(input[i]);
    if (!OpenATL::RGS::detail::Guid(token) || FAILED(::CLSIDFromString(token, &processClassId))) return 2;
    return (apartment ? processServer<true>(token) : processServer<false>(token)) ? 0 : 1;
}
