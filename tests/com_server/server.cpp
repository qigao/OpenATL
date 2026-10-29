#include <atlbase.h>
#include <openatl/com_server.h>
#include <openatl/com_registration.h>
#include "interfaces.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace OpenATL::COM;
#define REQUIRE(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "COM_SERVER_FAIL line=%d condition=%s winerr=%lu\n", \
                 __LINE__, #condition, static_cast<unsigned long>(::GetLastError())); \
    return false; } } while (0)

bool RunAtlCompatibilityTests();
bool RunAtlModuleTests();
bool RunRgsParserTests();
bool RunRgsRegistryTests();
namespace {
std::atomic<int> live{0};
bool throwUnexpected = false;
class Value final : public Object<Value, Interface<IOpenATLDerived, &IID_OpenATLDerived>,
                                       InterfaceAlias<IOpenATLValue, &IID_OpenATLValue, IOpenATLDerived>,
                                       Interface<IOpenATLSecond, &IID_OpenATLSecond>> {
public:
    explicit Value(Module& module) noexcept : Object(module) { ++live; }
    ~Value() { --live; }
    HRESULT STDMETHODCALLTYPE GetValue(LONG* value) noexcept override {
        if (!value) return E_POINTER;
        *value = 42; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSecond(LONG* value) noexcept override {
        if (!value) return E_POINTER;
        *value = 17; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetExtra(LONG* value) noexcept override {
        if (!value) return E_POINTER;
        *value = 99; return S_OK;
    }
};
class Throws final : public Object<Throws, Interface<IOpenATLValue, &IID_OpenATLValue>> {
public:
    explicit Throws(Module& module) : Object(module) {
        if (throwUnexpected) throw std::runtime_error("constructor failure");
        throw std::bad_alloc();
    }
    HRESULT STDMETHODCALLTYPE GetValue(LONG*) noexcept override { return E_UNEXPECTED; }
};

bool identities(IOpenATLValue* object) {
    ATL::CComPtr<IOpenATLSecond> second;
    REQUIRE(object->QueryInterface(IID_OpenATLSecond, reinterpret_cast<void**>(&second)) == S_OK);
    LONG value = 0;
    REQUIRE(second->GetSecond(&value) == S_OK && value == 17);
    ATL::CComPtr<IUnknown> a, b;
    REQUIRE(object->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&a)) == S_OK);
    REQUIRE(second->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&b)) == S_OK);
    REQUIRE(a.p == b.p);
    ATL::CComPtr<IOpenATLDerived> derived;
    REQUIRE(second->QueryInterface(IID_OpenATLDerived, reinterpret_cast<void**>(&derived)) == S_OK);
    REQUIRE(derived->GetExtra(&value) == S_OK && value == 99);
    ATL::CComPtr<IOpenATLValue> base;
    REQUIRE(derived->QueryInterface(IID_OpenATLValue, reinterpret_cast<void**>(&base)) == S_OK);
    REQUIRE(base.p == object);
    void* output = reinterpret_cast<void*>(1);
    REQUIRE(object->QueryInterface(IID_OpenATLUnknown, &output) == E_NOINTERFACE && output == nullptr);
    REQUIRE(object->QueryInterface(IID_IUnknown, nullptr) == E_POINTER);
    return true;
}

bool core() {
    REQUIRE(RunAtlCompatibilityTests());
    const ClassEntry entries[] = {{CLSID_OpenATLValue, &Value::CreateInstance},
                                  {CLSID_OpenATLThrow, &Throws::CreateInstance}};
    Module module(entries, 2);
    REQUIRE(module.Status() == S_OK && module.CanUnloadNow() == S_OK);
    REQUIRE(module.GetClassObject(CLSID_OpenATLValue, IID_IUnknown, nullptr) == E_POINTER);
    void* output = reinterpret_cast<void*>(1);
    REQUIRE(module.GetClassObject(CLSID_NULL, IID_IUnknown, &output) == CLASS_E_CLASSNOTAVAILABLE && !output);
    output = reinterpret_cast<void*>(1);
    REQUIRE(module.GetClassObject(CLSID_OpenATLValue, IID_OpenATLUnknown, &output) == E_NOINTERFACE && !output);
    REQUIRE(module.CanUnloadNow() == S_OK);
    ATL::CComPtr<IClassFactory> factory;
    REQUIRE(module.GetClassObject(CLSID_OpenATLValue, IID_IClassFactory,
                                  reinterpret_cast<void**>(&factory)) == S_OK);
    REQUIRE(module.CanUnloadNow() == S_FALSE);
    output = reinterpret_cast<void*>(1);
    REQUIRE(factory->CreateInstance(factory.p, IID_OpenATLValue, &output) == CLASS_E_NOAGGREGATION && !output);
    REQUIRE(factory->CreateInstance(nullptr, IID_OpenATLValue, nullptr) == E_POINTER);
    REQUIRE(factory->CreateInstance(nullptr, IID_OpenATLUnknown, &output) == E_NOINTERFACE && !output);
    REQUIRE(live == 0);
    ATL::CComPtr<IOpenATLValue> object;
    REQUIRE(factory->CreateInstance(nullptr, IID_OpenATLValue,
                                    reinterpret_cast<void**>(&object)) == S_OK);
    REQUIRE(live == 1 && identities(object));
    factory.Release();
    REQUIRE(module.CanUnloadNow() == S_FALSE);
    object.Release();
    REQUIRE(live == 0 && module.CanUnloadNow() == S_OK);

    REQUIRE(module.GetClassObject(CLSID_OpenATLThrow, IID_IClassFactory,
                                  reinterpret_cast<void**>(&factory)) == S_OK);
    output = reinterpret_cast<void*>(1);
    REQUIRE(factory->CreateInstance(nullptr, IID_OpenATLValue, &output) == E_OUTOFMEMORY && !output);
    throwUnexpected = true;
    output = reinterpret_cast<void*>(1);
    const HRESULT unexpected = factory->CreateInstance(nullptr, IID_OpenATLValue, &output);
    throwUnexpected = false;
    REQUIRE(unexpected == E_UNEXPECTED && !output);
    REQUIRE(factory->LockServer(FALSE) == E_UNEXPECTED);
    REQUIRE(factory->LockServer(TRUE) == S_OK);
    factory.Release();
    REQUIRE(module.CanUnloadNow() == S_FALSE);
    REQUIRE(module.GetClassObject(CLSID_OpenATLValue, IID_IClassFactory,
                                  reinterpret_cast<void**>(&factory)) == S_OK);
    REQUIRE(factory->LockServer(FALSE) == S_OK);
    factory.Release();
    REQUIRE(module.CanUnloadNow() == S_OK);

    std::atomic<bool> okay{true};
    ATL::CComPtr<IOpenATLValue> shared;
    REQUIRE(Value::CreateInstance(module, IID_OpenATLValue,
                                  reinterpret_cast<void**>(&shared)) == S_OK);
    std::vector<std::thread> workers;
    for (int i = 0; i < 8; ++i) workers.emplace_back([&] {
        for (int j = 0; j < 500; ++j) {
            ATL::CComPtr<IClassFactory> f;
            ATL::CComPtr<IOpenATLValue> v;
            if (module.GetClassObject(CLSID_OpenATLValue, IID_IClassFactory,
                                      reinterpret_cast<void**>(&f)) != S_OK ||
                f->CreateInstance(nullptr, IID_OpenATLValue,
                                   reinterpret_cast<void**>(&v)) != S_OK ||
                f->LockServer(TRUE) != S_OK || f->LockServer(FALSE) != S_OK) {
                okay = false; return;
            }
            v.p->AddRef(); v.p->Release();
            shared.p->AddRef(); shared.p->Release();
        }
    });
    for (auto& worker : workers) worker.join();
    shared.Release();
    REQUIRE(okay && live == 0 && module.CanUnloadNow() == S_OK);
    const ClassEntry duplicate[] = {entries[0], entries[0]};
    Module invalid(duplicate, 2);
    REQUIRE(invalid.Status() == E_INVALIDARG && invalid.CanUnloadNow() == S_FALSE);
    output = reinterpret_cast<void*>(1);
    REQUIRE(invalid.GetClassObject(CLSID_OpenATLValue, IID_IUnknown, &output) == E_INVALIDARG && !output);
    return true;
}

struct RegistryRoot {
    HKEY key = nullptr;
    std::wstring path;
    bool owned = false;
    bool Open() {
        GUID guid{};
        if (FAILED(::CoCreateGuid(&guid))) return false;
        wchar_t id[40]{};
        if (!::StringFromGUID2(guid, id, 40)) return false;
        path = std::wstring(L"Software\\OpenATL.COMServerTest.") + id;
        DWORD disposition = 0;
        LSTATUS status = ::RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr,
                                           REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr,
                                           &key, &disposition);
        owned = status == ERROR_SUCCESS && disposition == REG_CREATED_NEW_KEY;
        return owned;
    }
    bool Close() {
        if (key) { ::RegCloseKey(key); key = nullptr; }
        if (!owned) return true;
        // This unique, newly created test root owns all of its contents,
        // including the deliberately foreign-to-registrar regression entries.
        LSTATUS status = ::RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str());
        if (status == ERROR_SUCCESS) owned = false;
        return status == ERROR_SUCCESS;
    }
    ~RegistryRoot() { Close(); }
};

bool exists(HKEY root, LPCWSTR path) {
    ATL::CRegKey key;
    return ::RegOpenKeyExW(root, path, 0, KEY_READ, &key.m_hKey) == ERROR_SUCCESS;
}
bool registry() {
    RegistryRoot root;
    REQUIRE(root.Open());
    ClassRegistration entry{CLSID_OpenATLValue, L"C:\\OpenATL Conformance\\server.dll",
                             L"OpenATL.Private.Conformance", L"Native fixture", ThreadingModel::Both};
    wchar_t guid[40]{};
    REQUIRE(::StringFromGUID2(entry.clsid, guid, 40));
    const std::wstring clsid = std::wstring(L"CLSID\\") + guid;
    ATL::CRegKey conflict;
    REQUIRE(::RegCreateKeyExW(root.key, entry.progId, 0, nullptr, 0, KEY_ALL_ACCESS,
                              nullptr, &conflict.m_hKey, nullptr) == ERROR_SUCCESS);
    REQUIRE(conflict.SetStringValue(nullptr, L"Existing application") == ERROR_SUCCESS);
    REQUIRE(RegisterClass(root.key, entry) == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS));
    REQUIRE(!exists(root.key, clsid.c_str()) && exists(root.key, entry.progId));
    wchar_t foreign[64]{};
    ULONG capacity = 64;
    REQUIRE(conflict.QueryStringValue(nullptr, foreign, &capacity) == ERROR_SUCCESS &&
             std::wcscmp(foreign, L"Existing application") == 0);
    REQUIRE(::RegDeleteKeyW(root.key, entry.progId) == ERROR_SUCCESS);
    REQUIRE(conflict.Close() == ERROR_SUCCESS);

    REQUIRE(RegisterClass(root.key, entry) == S_OK);
    REQUIRE(exists(root.key, (clsid + L"\\InprocServer32").c_str()));
    REQUIRE(exists(root.key, L"OpenATL.Private.Conformance\\CLSID"));
    REQUIRE(RegisterClass(root.key, entry) == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS));
    ClassRegistration other = entry;
    other.modulePath = L"C:\\Other\\server.dll";
    REQUIRE(UnregisterClass(root.key, other) == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED));
    REQUIRE(exists(root.key, clsid.c_str()));

    ATL::CRegKey key;
    REQUIRE(::RegOpenKeyExW(root.key, clsid.c_str(), 0, KEY_ALL_ACCESS, &key.m_hKey) == ERROR_SUCCESS);
    DWORD marker = 17;
    REQUIRE(::RegSetValueExW(key.m_hKey, L"Foreign", 0, REG_DWORD,
                             reinterpret_cast<BYTE*>(&marker), sizeof(marker)) == ERROR_SUCCESS);
    REQUIRE(UnregisterClass(root.key, entry) == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED));
    REQUIRE(exists(root.key, (clsid + L"\\InprocServer32").c_str()));
    REQUIRE(::RegDeleteValueW(key.m_hKey, L"Foreign") == ERROR_SUCCESS);
    HKEY child = nullptr;
    REQUIRE(::RegCreateKeyExW(key.m_hKey, L"ForeignChild", 0, nullptr, 0, KEY_ALL_ACCESS,
                              nullptr, &child, nullptr) == ERROR_SUCCESS);
    ::RegCloseKey(child);
    REQUIRE(UnregisterClass(root.key, entry) == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED));
    REQUIRE(::RegDeleteKeyW(key.m_hKey, L"ForeignChild") == ERROR_SUCCESS);
    REQUIRE(key.Close() == ERROR_SUCCESS);
    REQUIRE(UnregisterClass(root.key, entry) == S_OK);
    REQUIRE(!exists(root.key, clsid.c_str()) && !exists(root.key, entry.progId));
    REQUIRE(UnregisterClass(root.key, entry) == S_OK);
    other = entry; other.modulePath = L"relative.dll";
    REQUIRE(RegisterClass(root.key, other) == E_INVALIDARG);
    other = entry; other.progId = L"CLSID";
    REQUIRE(RegisterClass(root.key, other) == E_INVALIDARG);
    other.progId = L"OpenATL_Invalid";
    REQUIRE(RegisterClass(root.key, other) == E_INVALIDARG);
    other.progId = L"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
    REQUIRE(RegisterClass(root.key, other) == E_INVALIDARG);
    other = entry; other.progId = nullptr; other.threadingModel = ThreadingModel::Apartment;
    REQUIRE(RegisterClass(root.key, other) == S_OK && UnregisterClass(root.key, other) == S_OK);
    // Multi-class source registration: validate all members before writes,
    // rollback a previously created class if a later one conflicts, and
    // preflight every owner before deleting any class from the batch.
    const ClassRegistration batch[] = {
        {CLSID_OpenATLValue, L"C:\\OpenATL Conformance\\server.dll",
         L"OpenATL.Private.BatchOne", L"Batch One", ThreadingModel::Both},
        {CLSID_OpenATLThrow, L"C:\\OpenATL Conformance\\server.dll",
         L"OpenATL.Private.BatchTwo", L"Batch Two", ThreadingModel::Apartment},
    };
    wchar_t batchGuid1[40]{}, batchGuid2[40]{};
    REQUIRE(::StringFromGUID2(batch[0].clsid, batchGuid1, 40));
    REQUIRE(::StringFromGUID2(batch[1].clsid, batchGuid2, 40));
    const std::wstring batchKey1 = std::wstring(L"CLSID\\") + batchGuid1;
    const std::wstring batchKey2 = std::wstring(L"CLSID\\") + batchGuid2;
    REQUIRE(RegisterClasses(root.key, batch, 0) == S_FALSE);
    REQUIRE(UnregisterClasses(root.key, batch, 0) == S_FALSE);
    REQUIRE(RegisterClasses(nullptr, batch, 2) == E_INVALIDARG);
    REQUIRE(RegisterClasses(root.key, nullptr, 2) == E_INVALIDARG);
    REQUIRE(RegisterClasses(root.key, batch, 1025) ==
            HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW));
    ClassRegistration invalid[] = {batch[0], batch[1]};
    invalid[1].modulePath = L"relative.dll";
    REQUIRE(RegisterClasses(root.key, invalid, 2) == E_INVALIDARG);
    invalid[1] = batch[0];
    REQUIRE(RegisterClasses(root.key, invalid, 2) == E_INVALIDARG);
    invalid[1] = batch[1];
    invalid[1].progId = L"openatl.private.batchone";
    REQUIRE(RegisterClasses(root.key, invalid, 2) == E_INVALIDARG);
    REQUIRE(!exists(root.key, batchKey1.c_str()) &&
            !exists(root.key, batchKey2.c_str()));

    ATL::CRegKey laterConflict;
    REQUIRE(::RegCreateKeyExW(root.key, batch[1].progId, 0, nullptr, 0,
                              KEY_ALL_ACCESS, nullptr, &laterConflict.m_hKey,
                              nullptr) == ERROR_SUCCESS);
    REQUIRE(laterConflict.SetStringValue(nullptr, L"Foreign application") == ERROR_SUCCESS);
    REQUIRE(RegisterClasses(root.key, batch, 2) ==
            HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS));
    REQUIRE(!exists(root.key, batchKey1.c_str()) &&
            !exists(root.key, batchKey2.c_str()));
    REQUIRE(exists(root.key, batch[1].progId));
    REQUIRE(laterConflict.Close() == ERROR_SUCCESS);
    REQUIRE(::RegDeleteKeyW(root.key, batch[1].progId) == ERROR_SUCCESS);

    REQUIRE(RegisterClasses(root.key, batch, 2) == S_OK);
    REQUIRE(exists(root.key, batchKey1.c_str()) && exists(root.key, batchKey2.c_str()));
    REQUIRE(RegisterClasses(root.key, batch, 2) ==
            HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS));

    ATL::CRegKey foreignInLater;
    REQUIRE(::RegOpenKeyExW(root.key, batchKey2.c_str(), 0, KEY_ALL_ACCESS,
                            &foreignInLater.m_hKey) == ERROR_SUCCESS);
    DWORD sentinel = 99;
    REQUIRE(::RegSetValueExW(foreignInLater.m_hKey, L"Foreign", 0, REG_DWORD,
                             reinterpret_cast<const BYTE*>(&sentinel),
                             sizeof(sentinel)) == ERROR_SUCCESS);
    REQUIRE(UnregisterClasses(root.key, batch, 2) ==
            HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED));
    REQUIRE(exists(root.key, batchKey1.c_str()) && exists(root.key, batchKey2.c_str()));
    REQUIRE(::RegDeleteValueW(foreignInLater.m_hKey, L"Foreign") == ERROR_SUCCESS);
    REQUIRE(foreignInLater.Close() == ERROR_SUCCESS);
    REQUIRE(UnregisterClasses(root.key, batch, 2) == S_OK);
    REQUIRE(!exists(root.key, batchKey1.c_str()) &&
            !exists(root.key, batchKey2.c_str()) &&
            !exists(root.key, batch[0].progId) &&
            !exists(root.key, batch[1].progId));
    REQUIRE(UnregisterClasses(root.key, batch, 2) == S_OK);
    REQUIRE(root.Close());
    return true;
}

class Dll final {
public:
    using GetClass = HRESULT (WINAPI*)(REFCLSID, REFIID, void**);
    using Operation = HRESULT (WINAPI*)();
    HMODULE image = nullptr;
    GetClass get = nullptr;
    Operation unload = nullptr, install = nullptr, uninstall = nullptr;
    bool installed = false;
    bool Open() {
        wchar_t path[32768]{};
        DWORD length = ::GetModuleFileNameW(nullptr, path, 32768);
        if (!length || length >= 32768) return false;
        wchar_t* leaf = std::wcsrchr(path, L'\\');
        const wchar_t name[] = L"openatl_com_server_fixture.dll";
        if (!leaf || size_t(leaf - path) + 1 + sizeof(name) / sizeof(wchar_t) > 32768) return false;
        std::memcpy(leaf + 1, name, sizeof(name));
        image = ::LoadLibraryW(path);
        if (!image) return false;
        get = reinterpret_cast<GetClass>(::GetProcAddress(image, "DllGetClassObject"));
        unload = reinterpret_cast<Operation>(::GetProcAddress(image, "DllCanUnloadNow"));
        install = reinterpret_cast<Operation>(::GetProcAddress(image, "DllRegisterServer"));
        uninstall = reinterpret_cast<Operation>(::GetProcAddress(image, "DllUnregisterServer"));
        return get && unload && install && uninstall;
    }
    bool Close() {
        if (installed) {
            if (uninstall() != S_OK) return false;
            installed = false;
        }
        if (image) {
            if (!unload || unload() != S_OK || !::FreeLibrary(image)) return false;
            image = nullptr;
        }
        return true;
    }
    ~Dll() { Close(); }
};

// All test-induced foreign registry content is owned under a newly-created
// exact ProgID. No preexisting user entry is overwritten or deleted.
struct ForeignAuxProgId {
    HKEY key = nullptr;
    bool owned = false;
    static constexpr LPCWSTR path = L"Software\\Classes\\OpenATL.Conformance.Aux.1";
    bool Create() noexcept {
        DWORD disposition = 0;
        const LSTATUS state = ::RegCreateKeyExW(HKEY_CURRENT_USER, path, 0,
            nullptr, 0, KEY_ALL_ACCESS, nullptr, &key, &disposition);
        if (state != ERROR_SUCCESS) return false;
        owned = disposition == REG_CREATED_NEW_KEY;
        return owned && ::RegSetValueExW(key, nullptr, 0, REG_SZ,
            reinterpret_cast<const BYTE*>(L"Foreign application"),
            sizeof(L"Foreign application")) == ERROR_SUCCESS;
    }
    bool Check() const noexcept {
        if (!key || !owned) return false;
        wchar_t value[64]{};
        DWORD type = 0, bytes = sizeof(value);
        return ::RegQueryValueExW(key, nullptr, nullptr, &type,
                   reinterpret_cast<BYTE*>(value), &bytes) == ERROR_SUCCESS &&
               type == REG_SZ && ::lstrcmpW(value, L"Foreign application") == 0;
    }
    bool Remove() noexcept {
        if (key) { ::RegCloseKey(key); key = nullptr; }
        if (!owned) return false;
        if (::RegDeleteKeyW(HKEY_CURRENT_USER, path) != ERROR_SUCCESS) return false;
        owned = false;
        return true;
    }
    ~ForeignAuxProgId() {
        if (key) ::RegCloseKey(key);
        if (owned) (void)::RegDeleteKeyW(HKEY_CURRENT_USER, path);
    }
};

bool native(DWORD apartment) {
    Dll dll;
    REQUIRE(dll.Open());
    ULONG_PTR token = 0;
    REQUIRE(::CoGetContextToken(&token) == CO_E_NOTINITIALIZED);
    REQUIRE(dll.unload() == S_OK);
    // Direct factory use is independent of COM initialization.
    ATL::CComPtr<IClassFactory> factory;
    REQUIRE(dll.get(CLSID_OpenATLValue, IID_IClassFactory,
                     reinterpret_cast<void**>(&factory)) == S_OK);
    REQUIRE(dll.unload() == S_FALSE);
    REQUIRE(factory->LockServer(TRUE) == S_OK);
    factory.Release();
    REQUIRE(dll.unload() == S_FALSE);
    REQUIRE(dll.get(CLSID_OpenATLValue, IID_IClassFactory,
                     reinterpret_cast<void**>(&factory)) == S_OK);
    REQUIRE(factory->LockServer(FALSE) == S_OK);
    REQUIRE(factory->LockServer(FALSE) == E_UNEXPECTED);
    factory.Release();
    REQUIRE(dll.unload() == S_OK);
    // First class is ordered before the auxiliary class by CLSID. A foreign
    // second ProgID forces failure after first-class installation, which
    // must be rolled back without modifying the foreign key.
    {
        ForeignAuxProgId foreign;
        REQUIRE(foreign.Create());
        REQUIRE(dll.install() == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS));
        REQUIRE(foreign.Check());
        wchar_t guid[40]{};
        REQUIRE(::StringFromGUID2(CLSID_OpenATLValue, guid, 40));
        const std::wstring firstKey = std::wstring(L"Software\\Classes\\CLSID\\") + guid;
        REQUIRE(!exists(HKEY_CURRENT_USER, firstKey.c_str()));
        REQUIRE(foreign.Remove());
    }
    REQUIRE(dll.install() == S_OK);
    dll.installed = true;
    REQUIRE(::CoGetContextToken(&token) == CO_E_NOTINITIALIZED);
    REQUIRE(dll.install() == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS));

    HRESULT initialized = ::CoInitializeEx(nullptr, apartment);
    REQUIRE(initialized == S_OK);
    struct ApartmentOwner { ~ApartmentOwner() { ::CoUninitialize(); } } owner;
    CLSID resolved{};
    REQUIRE(::CLSIDFromProgID(OpenATLTestProgId, &resolved) == S_OK &&
             IsEqualCLSID(resolved, CLSID_OpenATLValue));
    REQUIRE(::CLSIDFromProgID(OpenATLAuxProgId, &resolved) == S_OK &&
             IsEqualCLSID(resolved, CLSID_OpenATLAuxValue));
    ATL::CComPtr<IOpenATLValue> object;
    REQUIRE(::CoCreateInstance(CLSID_OpenATLValue, nullptr, CLSCTX_INPROC_SERVER,
                                IID_OpenATLValue, reinterpret_cast<void**>(&object)) == S_OK);
    LONG value = 0;
    REQUIRE(object->GetValue(&value) == S_OK && value == 42 && identities(object));
    ATL::CComPtr<IOpenATLValue> secondObject;
    REQUIRE(::CoCreateInstance(CLSID_OpenATLAuxValue, nullptr, CLSCTX_INPROC_SERVER,
                                IID_OpenATLValue, reinterpret_cast<void**>(&secondObject)) == S_OK);
    REQUIRE(secondObject->GetValue(&value) == S_OK && value == 84);
    REQUIRE(dll.unload() == S_FALSE);
    object.Release();
    secondObject.Release();
    // COM may cache a class factory until unused libraries are collected.
    ::CoFreeUnusedLibrariesEx(0, 0);
    REQUIRE(dll.unload() == S_OK);
    REQUIRE(dll.uninstall() == S_OK);
    dll.installed = false;
    void* missing = reinterpret_cast<void*>(1);
    REQUIRE(::CoCreateInstance(CLSID_OpenATLValue, nullptr, CLSCTX_INPROC_SERVER,
                                IID_OpenATLValue, &missing) == REGDB_E_CLASSNOTREG && !missing);
    missing = reinterpret_cast<void*>(1);
    REQUIRE(::CoCreateInstance(CLSID_OpenATLAuxValue, nullptr, CLSCTX_INPROC_SERVER,
                                IID_OpenATLValue, &missing) == REGDB_E_CLASSNOTREG && !missing);
    REQUIRE(dll.Close());
    REQUIRE(::GetModuleHandleW(L"openatl_com_server_fixture.dll") == nullptr);
    return true;
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    if (std::strcmp(argv[1], "--module") == 0) {
        if (!RunAtlModuleTests()) return 1;
        std::puts("ATL_MODULE_OK locks=1 termination=1 com_balance=1 class_objects=1 loop=1");
        return 0;
    }
    if (std::strcmp(argv[1], "--core") == 0) {
        ULONG_PTR token = 0;
        if (::CoGetContextToken(&token) != CO_E_NOTINITIALIZED ||
            !core() || !registry() || !RunRgsParserTests() ||
            !RunRgsRegistryTests() ||
            ::CoGetContextToken(&token) != CO_E_NOTINITIALIZED) return 1;
        std::puts("OPENATL_COM_SERVER_CORE_OK identity=1 factory=1 locks=1 concurrent=1 rollback=1 registry=1 no_com=1");
        return 0;
    }
    const bool sta = std::strcmp(argv[1], "--sta") == 0;
    if (!sta && std::strcmp(argv[1], "--mta") != 0) return 2;
    if (!native(sta ? COINIT_APARTMENTTHREADED : COINIT_MULTITHREADED)) return 1;
    ULONG_PTR token = 0;
    if (::CoGetContextToken(&token) != CO_E_NOTINITIALIZED) return 1;
    std::printf("OPENATL_COM_SERVER_NATIVE_OK mode=%s activation=1 identity=1 locks=1 unregister=1 unload=1\n",
                 sta ? "sta" : "mta");
    return 0;
}
