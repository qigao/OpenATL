#include <atlbase.h>
#include <openatl/com_server.h>
#include <openatl/com_registration.h>
#include <openatl/rgs_resource.h>
#include "interfaces.h"
#include "fixture_registry_ids.h"
#include "openatl_rgs_fixtures.generated.h"
#if defined(OPENATL_ORIGINAL_COMMAP_MIDL)
#include "COMMap.h"
#endif
#include <atomic>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <vector>
#include <sddl.h>

using namespace OpenATL::COM;
#define REQUIRE(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "COM_SERVER_FAIL line=%d condition=%s winerr=%lu\n", \
                 __LINE__, #condition, static_cast<unsigned long>(::GetLastError())); \
    return false; } } while (0)

bool RunAtlCompatibilityTests();
bool RunAtlDispatchTests();
bool RunAtlModuleTests();
bool RunRgsParserTests();
bool RunRgsRegistryTests();
#if defined(OPENATL_ORIGINAL_COMMAP_MIDL)
bool RunOriginalCommapTypeLibTest(const char* path);
#endif
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

#if defined(OPENATL_ORIGINAL_COMMAP_MIDL)
bool embeddedDispatch(Dll& dll, const char* creator = "OpenATLCreateEmbeddedDispatch") {
    using Create = HRESULT (WINAPI*)(IDispatch**);
    using TypeInfo = HRESULT (WINAPI*)(REFIID, REFGUID, ITypeInfo**);
    const auto create = reinterpret_cast<Create>(
        ::GetProcAddress(dll.image, creator));
    const auto query = reinterpret_cast<TypeInfo>(
        ::GetProcAddress(dll.image, "OpenATLGetEmbeddedTypeInfo"));
    REQUIRE(create && query);
    ATL::CComPtr<ITypeLib> registeredBefore;
    const HRESULT registryBefore = ::LoadRegTypeLib(LIBID_COMMAPLib, 1, 0,
        LOCALE_SYSTEM_DEFAULT, &registeredBefore.p);
    ATL::CComPtr<IDispatch> dispatch;
    REQUIRE(create(&dispatch.p) == S_OK && dispatch.p);
    REQUIRE(dll.unload() == S_FALSE);
    ATL::CComPtr<ITypeInfo> info;
    REQUIRE(dispatch->GetTypeInfo(0, LOCALE_SYSTEM_DEFAULT, &info.p) == S_OK && info.p);
    ATL::CComPtr<ITypeLib> library;
    UINT index = 0;
    REQUIRE(info->GetContainingTypeLib(&library.p, &index) == S_OK && library.p);
    TLIBATTR* attr = nullptr;
    REQUIRE(library->GetLibAttr(&attr) == S_OK && attr);
    const bool identity = IsEqualGUID(attr->guid, LIBID_COMMAPLib) &&
        attr->wMajorVerNum == 1 && attr->wMinorVerNum == 0 &&
        attr->syskind == (sizeof(void*) == 8 ? SYS_WIN64 : SYS_WIN32);
    library->ReleaseTLibAttr(attr);
    REQUIRE(identity);
    LPOLESTR names[] = {const_cast<LPOLESTR>(L"Name2")};
    DISPID member = DISPID_UNKNOWN;
    REQUIRE(dispatch->GetIDsOfNames(IID_NULL, names, 1,
        LOCALE_SYSTEM_DEFAULT, &member) == S_OK && member == 0);
    DISPPARAMS arguments{};
    ATL::CComVariant result;
    REQUIRE(dispatch->Invoke(member, IID_NULL, LOCALE_SYSTEM_DEFAULT,
        DISPATCH_PROPERTYGET, &arguments, &result, nullptr, nullptr) == S_OK);
    REQUIRE(result.vt == VT_BSTR && result.bstrVal &&
        ::SysStringLen(result.bstrVal) == 22 &&
        std::wcscmp(result.bstrVal, L"OpenWTL embedded chain") == 0);
    ITypeInfo* missing = reinterpret_cast<ITypeInfo*>(1);
    REQUIRE(query(IID_IStream, LIBID_COMMAPLib, &missing) == TYPE_E_ELEMENTNOTFOUND && !missing);
    missing = reinterpret_cast<ITypeInfo*>(1);
    REQUIRE(query(IID_IChain, GUID_NULL, &missing) == E_INVALIDARG && !missing);
    missing = reinterpret_cast<ITypeInfo*>(1);
    REQUIRE(query(IID_IChain, IID_IStream, &missing) == TYPE_E_ELEMENTNOTFOUND && !missing);
    REQUIRE(query(IID_IChain, LIBID_COMMAPLib, nullptr) == E_POINTER);
    ATL::CComPtr<ITypeLib> registeredAfter;
    REQUIRE(::LoadRegTypeLib(LIBID_COMMAPLib, 1, 0, LOCALE_SYSTEM_DEFAULT,
        &registeredAfter.p) == registryBefore);
    dispatch.Release();
    info.Release();
    library.Release();
    REQUIRE(dll.unload() == S_OK);
    std::puts("OPENATL_EMBEDDED_TYPELIB_OK image=dll dispatch=1 invoke=1 register=0");
    return true;
}

constexpr LPCWSTR typeLibKey = L"Software\\Classes\\TypeLib\\{4F9A68DD-DD9D-11CF-9462-00AA00BBAD7F}";
bool absentTypeLibRegistration() {
    const struct { LPCWSTR category; const GUID* id; } items[] = {
        {L"TypeLib", &LIBID_COMMAPLib}, {L"Interface", &IID_IChain},
        {L"Interface", &IID_IOuter}, {L"Interface", &IID_ITearOff1},
        {L"Interface", &IID_ITearOff2}
    };
    for (HKEY root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
        for (const auto& item : items) {
            wchar_t id[40]{};
            REQUIRE(::StringFromGUID2(*item.id, id, 40));
            const std::wstring path = std::wstring(L"Software\\Classes\\") + item.category + L"\\" + id;
            HKEY key = nullptr;
            const LSTATUS state = ::RegOpenKeyExW(root, path.c_str(), 0, KEY_READ, &key);
            if (key) ::RegCloseKey(key);
            REQUIRE(state == ERROR_FILE_NOT_FOUND || state == ERROR_PATH_NOT_FOUND);
        }
    }
    return true;
}

// Deny writes only to a newly-created test-owned TypeLib GUID. Keep the
// already-open security handle to restore its DACL on every exit path.
struct DeniedTypeLib {
    HKEY key = nullptr;
    bool owned = false, denied = false;
    std::vector<BYTE> security;
    bool Open() {
        DWORD disposition = 0;
        REQUIRE(::RegCreateKeyExW(HKEY_CURRENT_USER, typeLibKey, 0, nullptr, 0,
            KEY_ALL_ACCESS, nullptr, &key, &disposition) == ERROR_SUCCESS);
        owned = disposition == REG_CREATED_NEW_KEY;
        REQUIRE(owned);
        DWORD bytes = 0;
        REQUIRE(::RegGetKeySecurity(key, DACL_SECURITY_INFORMATION, nullptr,
            &bytes) == ERROR_INSUFFICIENT_BUFFER);
        security.resize(bytes);
        REQUIRE(::RegGetKeySecurity(key, DACL_SECURITY_INFORMATION,
            reinterpret_cast<PSECURITY_DESCRIPTOR>(security.data()), &bytes) == ERROR_SUCCESS);
        PSECURITY_DESCRIPTOR deny = nullptr;
        REQUIRE(::ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:P(D;;KW;;;WD)", SDDL_REVISION_1, &deny, nullptr));
        const LSTATUS state = ::RegSetKeySecurity(key,
            DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, deny);
        ::LocalFree(deny);
        denied = state == ERROR_SUCCESS;
        REQUIRE(denied);
        return true;
    }
    bool Close() {
        if (denied) {
            if (::RegSetKeySecurity(key, DACL_SECURITY_INFORMATION,
                reinterpret_cast<PSECURITY_DESCRIPTOR>(security.data())) != ERROR_SUCCESS) return false;
            denied = false;
        }
        if (key) { ::RegCloseKey(key); key = nullptr; }
        if (owned) {
            if (::RegDeleteKeyW(HKEY_CURRENT_USER, typeLibKey) != ERROR_SUCCESS) return false;
            owned = false;
        }
        return true;
    }
    ~DeniedTypeLib() { Close(); }
};

bool typeLibHelpers(Dll& dll) {
    bool perUser = true;
    REQUIRE(ATL::AtlGetPerUserRegistration(&perUser) == S_OK && !perUser);
    REQUIRE(ATL::AtlGetPerUserRegistration(nullptr) == E_POINTER);
    {
        ATL::CComBSTR path;
        ATL::CComPtr<ITypeLib> library;
        REQUIRE(ATL::AtlLoadTypeLib(dll.image, L"\\1", &path.m_str, &library.p) == S_OK);
        REQUIRE(path.m_str && library.p && ::SysStringLen(path.m_str) > 2);
        const UINT length = ::SysStringLen(path.m_str);
        REQUIRE(std::wcscmp(path.m_str + length - 2, L"\\1") == 0);
    }
    BSTR path = reinterpret_cast<BSTR>(1);
    ITypeLib* library = reinterpret_cast<ITypeLib*>(1);
    REQUIRE(ATL::AtlLoadTypeLib(dll.image, L"/1", &path, &library) == E_INVALIDARG && !path && !library);
    REQUIRE(FAILED(ATL::AtlLoadTypeLib(dll.image, L"\\999", &path, &library)) && !path && !library);
    REQUIRE(ATL::AtlLoadTypeLib(nullptr, nullptr, &path, &library) == E_INVALIDARG && !path && !library);
    REQUIRE(ATL::AtlLoadTypeLib(dll.image, nullptr, nullptr, &library) == E_POINTER && !library);
    return true;
}

bool typeLibRollback(Dll& dll) {
    REQUIRE(absentTypeLibRegistration()); // Never overwrite an installed sample.
    bool previous = false;
    REQUIRE(ATL::AtlGetPerUserRegistration(&previous) == S_OK);
    struct Restore { bool value; ~Restore() { ATL::AtlSetPerUserRegistration(value); } } restore{previous};
    REQUIRE(ATL::AtlSetPerUserRegistration(true) == S_OK);
    bool enabled = false;
    REQUIRE(ATL::AtlGetPerUserRegistration(&enabled) == S_OK && enabled);
    DeniedTypeLib denied;
    REQUIRE(denied.Open());
    const HRESULT nativeFailure = ATL::AtlRegisterTypeLib(dll.image, nullptr);
    REQUIRE(FAILED(nativeFailure) && nativeFailure != E_NOTIMPL);
    REQUIRE(dll.install() == nativeFailure);
    for (const GUID* id : {&CLSID_OpenATLValue, &CLSID_OpenATLAuxValue}) {
        wchar_t text[40]{};
        REQUIRE(::StringFromGUID2(*id, text, 40));
        const std::wstring path = std::wstring(L"Software\\Classes\\CLSID\\") + text;
        REQUIRE(!exists(HKEY_CURRENT_USER, path.c_str()));
    }
    REQUIRE(!exists(HKEY_CURRENT_USER, L"Software\\Classes\\OpenATL.Conformance.Server.1"));
    REQUIRE(!exists(HKEY_CURRENT_USER, L"Software\\Classes\\OpenATL.Conformance.Aux.1"));
    REQUIRE(denied.Close());
    REQUIRE(absentTypeLibRegistration());
    return true;
}
#endif

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


bool verifyResourceDocuments(HMODULE image) {
    using namespace OpenATL;
    const struct { unsigned id; std::wstring_view original; } cases[] = {
        {IDR_MICROSOFT_CHAIN, OpenATLRgsSamples::Chain},
        {IDR_MICROSOFT_OUTER, OpenATLRgsSamples::Outer}
    };
    for (const auto& sample : cases) {
        RGS::Document expected, actual;
        REQUIRE(RGS::Parse(sample.original, expected) == RGS::Error::Ok);
        REQUIRE(COM::ReadRgsResource(image, sample.id, actual) == S_OK);
        REQUIRE(actual.keys.size() == expected.keys.size());
        for (size_t i = 0; i < expected.keys.size(); ++i) {
            const auto& a = actual.keys[i];
            const auto& b = expected.keys[i];
            REQUIRE(a.path == b.path && a.directive == b.directive);
            REQUIRE(a.values.size() == b.values.size());
            for (size_t k = 0; k < b.values.size(); ++k)
                REQUIRE(a.values[k].name == b.values[k].name &&
                        a.values[k].text == b.values[k].text);
        }
    }
    RGS::Document untouched;
    REQUIRE(RGS::Parse(L"HKCR { Sentinel = s 'unchanged' }", untouched) ==
            RGS::Error::Ok);
    REQUIRE(FAILED(COM::ReadRgsResource(image, IDR_WRONG_RESOURCE_TYPE,
                                        untouched)));
    REQUIRE(FAILED(COM::ReadRgsResource(image, 32767, untouched)));
    REQUIRE(COM::ReadRgsResource(nullptr, IDR_MICROSOFT_CHAIN,
                                 untouched) == E_INVALIDARG);
    REQUIRE(COM::ReadRgsResource(image, 0, untouched) == E_INVALIDARG);
    REQUIRE(untouched.keys.size() == 1 &&
            untouched.keys[0].path == L"Sentinel");
    std::wstring decoded = L"untouched";
    const BYTE invalidUtf8[] = {0xef, 0xbb, 0xbf, 0xc3, 0x28};
    REQUIRE(COM::rgs_resource_detail::Decode(
        invalidUtf8, sizeof(invalidUtf8), decoded) ==
        HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION));
    REQUIRE(decoded == L"untouched");
    const BYTE invalidUtf16[] = {0xff, 0xfe, 0x00, 0xd8};
    REQUIRE(COM::rgs_resource_detail::Decode(
        invalidUtf16, sizeof(invalidUtf16), decoded) ==
        HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION));
    REQUIRE(decoded == L"untouched");
    const BYTE validUtf16[] = {0xff, 0xfe, 0x48, 0x00, 0x4b, 0x00,
                               0x43, 0x00, 0x52, 0x00, 0x20, 0x00,
                               0x7b, 0x00, 0x20, 0x00, 0x41, 0x00,
                               0x20, 0x00, 0x7d, 0x00};
    REQUIRE(COM::rgs_resource_detail::Decode(
        validUtf16, sizeof(validUtf16), decoded) == S_OK &&
        decoded == L"HKCR { A }");
    REQUIRE(COM::UpdateRegistryResource(IDR_MICROSOFT_CHAIN, TRUE) ==
            E_UNEXPECTED); // No ambient registration context.
    // Read-only resource admission must not create a COM apartment.
    ULONG_PTR token = 0;
    REQUIRE(::CoGetContextToken(&token) == CO_E_NOTINITIALIZED);
    return true;
}

bool scopedResourceRegistry(HMODULE image) {
    // The EXE can explicitly borrow its own GUID-isolated root for testing;
    // it does not inherit a DLL's TLS context or write to default HKCR.
    RegistryRoot root;
    REQUIRE(root.Open());
    using OpenATL::COM::ScopedRgsRegistrationContext;
    {
        ScopedRgsRegistrationContext invalid(HKEY_CURRENT_USER, image,
                                              L"RgsNativeTest");
        REQUIRE(invalid.Status() == E_INVALIDARG);
        ScopedRgsRegistrationContext invalidOwner(root.key, image,
                                                   L"illegal\\owner");
        REQUIRE(invalidOwner.Status() == E_INVALIDARG);
    }
    {
        ScopedRgsRegistrationContext context(root.key, image,
                                              L"RgsNativeTest");
        REQUIRE(context.Status() == S_OK);
        ScopedRgsRegistrationContext nested(root.key, image, L"Nested");
        REQUIRE(nested.Status() == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS));
        REQUIRE(OpenATL::COM::UpdateRegistryResource(
            IDR_MICROSOFT_CHAIN, TRUE) == S_OK);
        REQUIRE(exists(root.key, L"COMMap.Chain.1"));
        REQUIRE(OpenATL::COM::UpdateRegistryResource(
            IDR_MICROSOFT_CHAIN, FALSE) == S_OK);
        REQUIRE(!exists(root.key, L"COMMap.Chain.1"));
        REQUIRE(exists(root.key, L"CLSID")); // NoRemove container remains.
        REQUIRE(OpenATL::COM::UpdateRegistryResource(
            IDR_MICROSOFT_CHAIN, FALSE) == S_OK);
    }
    REQUIRE(OpenATL::COM::UpdateRegistryResource(
        IDR_MICROSOFT_CHAIN, TRUE) == E_UNEXPECTED);
    REQUIRE(root.Close());
    return true;
}

bool native(DWORD apartment) {
    Dll dll;
    REQUIRE(dll.Open());
    REQUIRE(verifyResourceDocuments(dll.image));
    REQUIRE(scopedResourceRegistry(dll.image));
#if defined(OPENATL_ORIGINAL_COMMAP_MIDL)
    REQUIRE(typeLibHelpers(dll));
    REQUIRE(typeLibRollback(dll));
#endif
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
#if defined(OPENATL_ORIGINAL_COMMAP_MIDL)
    REQUIRE(embeddedDispatch(dll));
    REQUIRE(embeddedDispatch(dll, "OpenATLCreateRegisteredDispatch"));
    REQUIRE(exists(HKEY_CURRENT_USER, typeLibKey));
    ATL::CComPtr<ITypeLib> registeredTypeLib;
    REQUIRE(::LoadRegTypeLib(LIBID_COMMAPLib, 1, 0, LOCALE_SYSTEM_DEFAULT,
        &registeredTypeLib.p) == S_OK && registeredTypeLib.p);
    registeredTypeLib.Release();
#endif
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
#if defined(OPENATL_ORIGINAL_COMMAP_MIDL)
    REQUIRE(absentTypeLibRegistration());
#endif
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
#if defined(OPENATL_ORIGINAL_COMMAP_MIDL)
    if (argc == 3 && std::strcmp(argv[1], "--commap-tlb") == 0) {
        if (!RunOriginalCommapTypeLibTest(argv[2])) return 1;
        std::puts("OPENATL_ORIGINAL_COMMAP_MIDL_OK source=1 typelib=1 iids=1 dispids=1 register=0");
        return 0;
    }
#endif
    if (argc != 2) return 2;
    if (std::strcmp(argv[1], "--module") == 0) {
        if (!RunAtlModuleTests()) return 1;
        std::puts("ATL_MODULE_OK locks=1 termination=1 com_balance=1 class_objects=1 loop=1");
        return 0;
    }
    if (std::strcmp(argv[1], "--core") == 0) {
        ULONG_PTR token = 0;
        if (::CoGetContextToken(&token) != CO_E_NOTINITIALIZED ||
            !core() || !RunAtlDispatchTests() || !registry() || !RunRgsParserTests() ||
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
