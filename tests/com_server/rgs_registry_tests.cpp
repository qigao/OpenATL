// Real native Windows HKCU RGS execution acceptance.
//
// All registry writes are confined to a newly owned, GUID-named subtree of
// HKCU\Software. RegDeleteTree is used ONLY by this test fixture to clean
// that isolated subtree. Production RGS execution uses nonrecursive deletes.
#if !defined(OPENATL_RGS_TEST_FAULT)
#error Native RGS fault admission must be configured for every test translation unit
#endif
#include <openatl/rgs_registration.h>
#include "openatl_rgs_fixtures.generated.h"

#include <atlbase.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
using OpenATL::RGS::Document;
using OpenATL::RGS::Directive;
using OpenATL::RGS::Error;
using OpenATL::COM::RegisterRgs;
using OpenATL::COM::UnregisterRgs;
using OpenATL::COM::rgs_registration_detail::FailedWriteAtNode;

#define REG_CHECK(expr) do { if (!(expr)) { \
    std::fprintf(stderr, "RGS_REGISTRY_FAIL line=%d expr=%s winerr=%lu\n", \
        __LINE__, #expr, static_cast<unsigned long>(::GetLastError())); \
    return false; } } while (0)

constexpr wchar_t kModule[] = L"C:\\OpenATL Conformance\\RGS server.dll";
constexpr const wchar_t* kOwners[] = {
    L"MsChain", L"MsOuter", L"MsArray", L"MsCollection"
};

struct TestRoot final {
    HKEY handle = nullptr;
    std::wstring path;
    bool owned = false;
    bool Open() {
        GUID guid{};
        if (FAILED(::CoCreateGuid(&guid))) return false;
        wchar_t chars[40]{};
        if (!::StringFromGUID2(guid, chars, 40)) return false;
        path = std::wstring(L"Software\\OpenATL.RGSTest.") + chars;
        DWORD disposition = 0;
        const LSTATUS state = ::RegCreateKeyExW(
            HKEY_CURRENT_USER, path.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
            KEY_ALL_ACCESS, nullptr, &handle, &disposition);
        if (state != ERROR_SUCCESS) return false;
        owned = disposition == REG_CREATED_NEW_KEY;
        return owned;
    }
    bool Close() {
        if (handle) { ::RegCloseKey(handle); handle = nullptr; }
        if (!owned) return false;
        const LSTATUS status = ::RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str());
        if (status == ERROR_SUCCESS) owned = false;
        return status == ERROR_SUCCESS;
    }
    ~TestRoot() {
        if (handle) ::RegCloseKey(handle);
        if (owned) (void)::RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str());
    }
};

bool Exists(HKEY root, const std::wstring& path) {
    HKEY key = nullptr;
    LSTATUS status = ::RegOpenKeyExW(root, path.c_str(), 0, KEY_READ, &key);
    if (key) ::RegCloseKey(key);
    return status == ERROR_SUCCESS;
}
bool ReadValue(HKEY root, const std::wstring& path,
               const std::wstring& name, std::wstring& result) {
    HKEY key = nullptr;
    if (::RegOpenKeyExW(root, path.c_str(), 0, KEY_QUERY_VALUE, &key)
        != ERROR_SUCCESS) return false;
    DWORD type = 0, bytes = 0;
    LSTATUS status = ::RegQueryValueExW(key, name.c_str(),
                                       nullptr, &type, nullptr, &bytes);
    if (status != ERROR_SUCCESS || type != REG_SZ ||
        bytes == 0 || bytes > 16384) {
        ::RegCloseKey(key); return false;
    }
    std::vector<wchar_t> chars(bytes / sizeof(wchar_t) + 1);
    status = ::RegQueryValueExW(key, name.c_str(), nullptr, &type,
             reinterpret_cast<BYTE*>(chars.data()), &bytes);
    ::RegCloseKey(key);
    if (status != ERROR_SUCCESS || type != REG_SZ ||
        bytes % sizeof(wchar_t) != 0 || chars[bytes / sizeof(wchar_t) - 1] != 0)
        return false;
    result.assign(chars.data(), bytes / sizeof(wchar_t) - 1);
    return true;
}
bool WriteValue(HKEY root, const std::wstring& path,
                LPCWSTR name, LPCWSTR value) {
    HKEY key = nullptr;
    if (::RegOpenKeyExW(root, path.c_str(), 0, KEY_SET_VALUE, &key)
        != ERROR_SUCCESS) return false;
    LSTATUS result = ::RegSetValueExW(key, name, 0, REG_SZ,
        reinterpret_cast<const BYTE*>(value),
        static_cast<DWORD>((std::wcslen(value) + 1) * sizeof(wchar_t)));
    ::RegCloseKey(key);
    return result == ERROR_SUCCESS;
}
bool AllOwnedAbsent(HKEY root, const Document& doc) {
    for (const auto& key : doc.keys) {
        if (key.directive == Directive::NoRemove) continue;
        if (Exists(root, key.path)) return false;
    }
    return true;
}
bool CompareProjection(HKEY root, const Document& source,
                       const wchar_t* owner) {
    Document expanded;
    if (OpenATL::RGS::ResolveModule(source, kModule, expanded) != Error::Ok)
        return false;
    const std::wstring marker = std::wstring(L"RGS1:") + owner + L":" + kModule;
    for (const auto& key : expanded.keys) {
        if (!Exists(root, key.path)) return false;
        std::wstring actual;
        const bool shared = key.directive == Directive::NoRemove;
        if (shared) {
            HKEY h = nullptr;
            if (::RegOpenKeyExW(root, key.path.c_str(), 0, KEY_QUERY_VALUE, &h)
                != ERROR_SUCCESS) return false;
            DWORD bytes = 0;
            const LSTATUS status = ::RegQueryValueExW(
                h, L"OpenATL.COMOwner", nullptr, nullptr, nullptr, &bytes);
            ::RegCloseKey(h);
            if (status != ERROR_FILE_NOT_FOUND) return false;
        } else {
            if (!ReadValue(root, key.path, L"OpenATL.COMOwner", actual) ||
                actual != marker) return false;
        }
        for (const auto& expected : key.values) {
            if (!ReadValue(root, key.path, expected.name, actual) ||
                actual != expected.text) return false;
        }
    }
    return true;
}
bool TestCorpus() {
    TestRoot root;
    REG_CHECK(root.Open());
    constexpr size_t n = sizeof(OpenATLRgsSamples::All) /
                         sizeof(OpenATLRgsSamples::All[0]);
    std::vector<Document> documents;
    for (size_t i = 0; i < n; ++i) {
        Document d;
        REG_CHECK(OpenATL::RGS::Parse(
            OpenATLRgsSamples::All[i].content, d) == Error::Ok);
        REG_CHECK(RegisterRgs(root.handle, d, kModule, kOwners[i]) == S_OK);
        REG_CHECK(CompareProjection(root.handle, d, kOwners[i]));
        documents.push_back(std::move(d));
    }
    // All four different original ATL classes coexist under one shared CLSID.
    REG_CHECK(Exists(root.handle, L"CLSID"));
    for (size_t i = n; i-- != 0;) {
        REG_CHECK(CompareProjection(root.handle, documents[i], kOwners[i]));
        REG_CHECK(UnregisterRgs(root.handle, documents[i], kModule,
                                kOwners[i]) == S_OK);
        REG_CHECK(AllOwnedAbsent(root.handle, documents[i]));
        REG_CHECK(UnregisterRgs(root.handle, documents[i], kModule,
                                kOwners[i]) == S_OK);
    }
    REG_CHECK(Exists(root.handle, L"CLSID")); // NoRemove shared container retained.
    REG_CHECK(root.Close());
    return true;
}
bool TestForeignContent() {
    TestRoot root;
    REG_CHECK(root.Open());
    Document doc;
    REG_CHECK(OpenATL::RGS::Parse(
        OpenATLRgsSamples::Chain, doc) == Error::Ok);
    const std::wstring foreignPath = doc.keys[2].path;
    HKEY foreign = nullptr;
    REG_CHECK(::RegCreateKeyExW(root.handle, foreignPath.c_str(), 0, nullptr,
        0, KEY_ALL_ACCESS, nullptr, &foreign, nullptr) == ERROR_SUCCESS);
    constexpr wchar_t label[] = L"Foreign value";
    REG_CHECK(::RegSetValueExW(foreign, nullptr, 0, REG_SZ,
       reinterpret_cast<const BYTE*>(label), sizeof(label)) == ERROR_SUCCESS);
    ::RegCloseKey(foreign);
    REG_CHECK(RegisterRgs(root.handle, doc, kModule, L"MsChain") ==
              HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS));
    REG_CHECK(AllOwnedAbsent(root.handle, doc) == false); // Foreign key exists.
    std::wstring actual;
    REG_CHECK(ReadValue(root.handle, foreignPath, L"", actual) &&
              actual == label);
    REG_CHECK(!Exists(root.handle, doc.keys.front().path));
    REG_CHECK(::RegDeleteKeyW(root.handle, foreignPath.c_str()) == ERROR_SUCCESS);

    REG_CHECK(RegisterRgs(root.handle, doc, kModule, L"MsChain") == S_OK);
    const std::wstring last = doc.keys.back().path;
    HKEY key = nullptr;
    REG_CHECK(::RegOpenKeyExW(root.handle, last.c_str(), 0, KEY_ALL_ACCESS,
                              &key) == ERROR_SUCCESS);
    DWORD foreignNumber = 123;
    REG_CHECK(::RegSetValueExW(key, L"Foreign", 0, REG_DWORD,
        reinterpret_cast<const BYTE*>(&foreignNumber),
        sizeof(foreignNumber)) == ERROR_SUCCESS);
    REG_CHECK(UnregisterRgs(root.handle, doc, kModule, L"MsChain") ==
              HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED));
    REG_CHECK(Exists(root.handle, doc.keys.front().path)); // Full-plan preflight.
    REG_CHECK(::RegDeleteValueW(key, L"Foreign") == ERROR_SUCCESS);
    ::RegCloseKey(key);

    const std::wstring ownerPath = doc.keys[2].path;
    std::wstring recorded;
    REG_CHECK(ReadValue(root.handle, ownerPath, L"OpenATL.COMOwner", recorded));
    REG_CHECK(WriteValue(root.handle, ownerPath, L"OpenATL.COMOwner",
                         L"other-owner"));
    REG_CHECK(UnregisterRgs(root.handle, doc, kModule, L"MsChain") ==
              HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED));
    REG_CHECK(Exists(root.handle, doc.keys.front().path));
    REG_CHECK(WriteValue(root.handle, ownerPath, L"OpenATL.COMOwner",
                         recorded.c_str()));

    const std::wstring parentPath = doc.keys.front().path;
    HKEY stranger = nullptr;
    const std::wstring child = parentPath + L"\\InjectedChild";
    REG_CHECK(::RegCreateKeyExW(root.handle, child.c_str(), 0, nullptr,
        0, KEY_ALL_ACCESS, nullptr, &stranger, nullptr) == ERROR_SUCCESS);
    ::RegCloseKey(stranger);
    REG_CHECK(UnregisterRgs(root.handle, doc, kModule, L"MsChain") ==
              HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED));
    REG_CHECK(Exists(root.handle, doc.keys.front().path));
    REG_CHECK(::RegDeleteKeyW(root.handle, child.c_str()) == ERROR_SUCCESS);
    REG_CHECK(UnregisterRgs(root.handle, doc, kModule, L"MsChain") == S_OK);
    REG_CHECK(AllOwnedAbsent(root.handle, doc));
    Document shared;
    REG_CHECK(OpenATL::RGS::Parse(L"HKCR { NoRemove AppID = s 'not a value-free container' }", shared) == Error::Ok);
    REG_CHECK(RegisterRgs(root.handle, shared, kModule, L"AppIdGuard") == E_INVALIDARG);
    REG_CHECK(OpenATL::RGS::Parse(L"HKCR { A { NoRemove AppID { B = s 'nested' } } }", shared) == Error::Ok);
    REG_CHECK(RegisterRgs(root.handle, shared, kModule, L"AppIdGuard") == E_INVALIDARG);
    REG_CHECK(!Exists(root.handle, L"A") && !Exists(root.handle, L"AppID"));
    REG_CHECK(root.Close());
    return true;
}
bool TestFaultRollback() {
    TestRoot root;
    REG_CHECK(root.Open());
    Document doc;
    REG_CHECK(OpenATL::RGS::Parse(
        OpenATLRgsSamples::MapCollection, doc) == Error::Ok);
    for (size_t index = 0; index < doc.keys.size(); ++index) {
        if (doc.keys[index].directive == Directive::NoRemove) continue;
        FailedWriteAtNode() = static_cast<int>(index);
        const HRESULT hr = RegisterRgs(root.handle, doc, kModule, L"MsFault");
        FailedWriteAtNode() = -1;
        REG_CHECK(hr == HRESULT_FROM_WIN32(ERROR_WRITE_FAULT));
        REG_CHECK(AllOwnedAbsent(root.handle, doc));
    }
    REG_CHECK(RegisterRgs(root.handle, doc, kModule, L"MsFault") == S_OK);
    REG_CHECK(UnregisterRgs(root.handle, doc, kModule, L"MsFault") == S_OK);
    REG_CHECK(root.Close());
    return true;
}
bool TestBadInputs() {
    TestRoot root;
    REG_CHECK(root.Open());
    Document doc;
    REG_CHECK(OpenATL::RGS::Parse(
        OpenATLRgsSamples::Chain, doc) == Error::Ok);
    REG_CHECK(RegisterRgs(HKEY_CURRENT_USER, doc, kModule, L"MsChain") ==
              E_INVALIDARG);
    REG_CHECK(RegisterRgs(root.handle, doc, L"relative.dll", L"MsChain") ==
              E_INVALIDARG);
    REG_CHECK(RegisterRgs(root.handle, doc, kModule, L"") == E_INVALIDARG);
    REG_CHECK(RegisterRgs(root.handle, doc, kModule, L"bad\\owner") ==
              E_INVALIDARG);
    Document corrupted = doc;
    corrupted.keys[0].path = L"..\\Foreign";
    REG_CHECK(RegisterRgs(root.handle, corrupted, kModule, L"MsChain") ==
              E_INVALIDARG);
    corrupted = doc;
    corrupted.keys[0].directive = Directive::NoRemove;
    REG_CHECK(RegisterRgs(root.handle, corrupted, kModule, L"MsChain") ==
              E_INVALIDARG);
    corrupted = doc;
    corrupted.keys[0].values.push_back(
        {L"OpenATL.COMOwner", L"forge"});
    REG_CHECK(RegisterRgs(root.handle, corrupted, kModule, L"MsChain") ==
              E_INVALIDARG);
    REG_CHECK(AllOwnedAbsent(root.handle, doc));
    REG_CHECK(root.Close());
    return true;
}
} // namespace

bool RunRgsRegistryTests() {
    ULONG_PTR token = 0;
    if (::CoGetContextToken(&token) != CO_E_NOTINITIALIZED) return false;
    if (!TestCorpus() || !TestForeignContent() ||
        !TestFaultRollback() || !TestBadInputs()) return false;
    return ::CoGetContextToken(&token) == CO_E_NOTINITIALIZED;
}
