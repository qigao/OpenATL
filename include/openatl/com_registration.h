// Explicit, non-overwriting COM registration. No RGS or implicit elevation.
#ifndef OPENATL_COM_REGISTRATION_H
#define OPENATL_COM_REGISTRATION_H

#include <windows.h>
#include <objbase.h>
#include <array>
#include <cstring>
#include <cwchar>
#include <new>
#include <string>
#include <vector>

namespace OpenATL { namespace COM {

enum class ThreadingModel { Apartment, Both, Free };
struct ClassRegistration {
    CLSID clsid;
    LPCWSTR modulePath;
    LPCWSTR progId = nullptr;
    LPCWSTR description = L"";
    ThreadingModel threadingModel = ThreadingModel::Apartment;
};

namespace registration_detail {
class Key final {
public:
    HKEY value = nullptr;
    Key() = default;
    ~Key() { if (value) ::RegCloseKey(value); }
    Key(const Key&) = delete;
    Key& operator=(const Key&) = delete;
};
inline HRESULT result(LSTATUS error) noexcept { return HRESULT_FROM_WIN32(error); }
inline bool bounded(LPCWSTR value, size_t limit) noexcept {
    if (!value) return false;
    for (size_t i = 0; i <= limit; ++i) if (value[i] == 0) return true;
    return false;
}
inline bool validProgId(LPCWSTR text) noexcept {
    if (!bounded(text, 39) || !*text) return false;
    const auto alpha = [](wchar_t c) {
        return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z');
    };
    if (!alpha(*text)) return false;
    for (const wchar_t* p = text; *p; ++p)
        if (!alpha(*p) && !(*p >= L'0' && *p <= L'9') && *p != L'.')
            return false;
    return ::lstrcmpiW(text, L"CLSID") && ::lstrcmpiW(text, L"Interface") &&
           ::lstrcmpiW(text, L"TypeLib") && ::lstrcmpiW(text, L"AppID");
}
struct Value { std::wstring name; std::wstring text; };
struct Node { std::wstring path; std::vector<Value> values; };
struct Plan {
    std::vector<Node> nodes;
    HRESULT Build(const ClassRegistration& entry) {
        if (IsEqualCLSID(entry.clsid, CLSID_NULL) ||
            !bounded(entry.modulePath, 32000) || !*entry.modulePath ||
            !bounded(entry.description, 4096) ||
            (entry.progId && !validProgId(entry.progId))) return E_INVALIDARG;
        const wchar_t* model = nullptr;
        switch (entry.threadingModel) {
        case ThreadingModel::Apartment: model = L"Apartment"; break;
        case ThreadingModel::Both: model = L"Both"; break;
        case ThreadingModel::Free: model = L"Free"; break;
        default: return E_INVALIDARG;
        }
        const size_t length = std::wcslen(entry.modulePath);
        const bool drive = length >= 3 &&
            ((entry.modulePath[0] >= L'A' && entry.modulePath[0] <= L'Z') ||
             (entry.modulePath[0] >= L'a' && entry.modulePath[0] <= L'z')) &&
            entry.modulePath[1] == L':' &&
            (entry.modulePath[2] == L'\\' || entry.modulePath[2] == L'/');
        const bool unc = length >= 3 && entry.modulePath[0] == L'\\' &&
                         entry.modulePath[1] == L'\\';
        if (!drive && !unc) return E_INVALIDARG;
        DWORD capacity = ::GetFullPathNameW(entry.modulePath, 0, nullptr, nullptr);
        if (!capacity) return result(::GetLastError());
        if (capacity > 32001) return E_INVALIDARG;
        std::wstring path(capacity, L'\0');
        DWORD size = ::GetFullPathNameW(entry.modulePath, capacity, &path[0], nullptr);
        if (!size) return result(::GetLastError());
        if (size >= capacity) return result(ERROR_INSUFFICIENT_BUFFER);
        path.resize(size);
        wchar_t guid[40]{};
        if (!::StringFromGUID2(entry.clsid, guid, 40)) return E_INVALIDARG;
        const std::wstring classPath = std::wstring(L"CLSID\\") + guid;
        const std::wstring owner = std::wstring(L"1:") + guid + L":" + path;
        nodes.reserve(5);
        // The ownership value is written before ordinary class data, so an
        // interrupted partial installation can still be identified explicitly.
        nodes.push_back({classPath, {{L"OpenATL.COMOwner", owner},
                                     {L"", entry.description}}});
        nodes.push_back({classPath + L"\\InprocServer32",
                         {{L"", path}, {L"ThreadingModel", model}}});
        if (entry.progId) {
            nodes.push_back({classPath + L"\\ProgID", {{L"", entry.progId}}});
            nodes.push_back({entry.progId, {{L"OpenATL.COMOwner", owner},
                                           {L"", entry.description}}});
            nodes.push_back({std::wstring(entry.progId) + L"\\CLSID", {{L"", guid}}});
        }
        return S_OK;
    }
};

inline bool childOf(const std::wstring& parent, const std::wstring& child) noexcept {
    const wchar_t* separator = std::wcsrchr(child.c_str(), L'\\');
    return separator && size_t(separator - child.c_str()) == parent.size() &&
           ::CompareStringOrdinal(child.c_str(), static_cast<int>(parent.size()),
                                  parent.c_str(), static_cast<int>(parent.size()),
                                  TRUE) == CSTR_EQUAL;
}

// Inspect before deleting anything: reject unknown values/children and changed
// values. Missing expected values are allowed for interrupted installations.
template<class Selection>
inline LSTATUS inspect(HKEY root, const Plan& plan, size_t index,
                       const Selection& selected, bool requireOwner,
                       bool requireAllValues = false) noexcept {
    Key key;
    LSTATUS status = ::RegOpenKeyExW(root, plan.nodes[index].path.c_str(), 0,
                                    KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS, &key.value);
    if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) return ERROR_SUCCESS;
    if (status != ERROR_SUCCESS) return status;
    const Node& node = plan.nodes[index];
    bool ownerFound = false;
    for (DWORD i = 0;; ++i) {
        wchar_t name[256]{};
        DWORD size = 256;
        status = ::RegEnumValueW(key.value, i, name, &size, nullptr, nullptr, nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS) break;
        if (status == ERROR_MORE_DATA) return ERROR_ACCESS_DENIED;
        if (status != ERROR_SUCCESS) return status;
        const Value* expected = nullptr;
        for (const Value& value : node.values)
            if (::lstrcmpiW(value.name.c_str(), name) == 0) expected = &value;
        if (!expected) return ERROR_ACCESS_DENIED;
        DWORD type = 0, bytes = 0;
        status = ::RegQueryValueExW(key.value, name, nullptr, &type, nullptr, &bytes);
        const DWORD expectedBytes = static_cast<DWORD>((expected->text.size() + 1) * sizeof(wchar_t));
        if (status != ERROR_SUCCESS) return status;
        if (type != REG_SZ || bytes != expectedBytes) return ERROR_ACCESS_DENIED;
        wchar_t text[32768]{};
        if (bytes > sizeof(text)) return ERROR_ACCESS_DENIED;
        status = ::RegQueryValueExW(key.value, name, nullptr, &type,
                                    reinterpret_cast<BYTE*>(text), &bytes);
        if (status != ERROR_SUCCESS) return status;
        if (type != REG_SZ || bytes != expectedBytes ||
            std::memcmp(text, expected->text.c_str(), bytes) != 0) return ERROR_ACCESS_DENIED;
        if (::lstrcmpiW(name, L"OpenATL.COMOwner") == 0) ownerFound = true;
    }
    bool ownsRoot = false;
    for (const Value& value : node.values)
        if (::lstrcmpiW(value.name.c_str(), L"OpenATL.COMOwner") == 0) ownsRoot = true;
    if (requireOwner && ownsRoot && !ownerFound) return ERROR_ACCESS_DENIED;
    if (requireAllValues) {
        for (const Value& value : node.values) {
            // Ordinary unregistration must see the complete expected key.
            // Rollback of a partial install deliberately leaves this off.
            DWORD type = 0, bytes = 0;
            status = ::RegQueryValueExW(key.value, value.name.c_str(), nullptr,
                                        &type, nullptr, &bytes);
            if (status != ERROR_SUCCESS) return ERROR_ACCESS_DENIED;
        }
    }
    for (DWORD i = 0;; ++i) {
        wchar_t name[256]{};
        DWORD size = 256;
        status = ::RegEnumKeyExW(key.value, i, name, &size, nullptr, nullptr, nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS) break;
        if (status != ERROR_SUCCESS) return status;
        bool expected = false;
        for (size_t j = 0; j < plan.nodes.size(); ++j) {
            const std::wstring& child = plan.nodes[j].path;
            if (selected[j] && childOf(node.path, child) &&
                ::lstrcmpiW(std::wcsrchr(child.c_str(), L'\\') + 1, name) == 0) expected = true;
        }
        if (!expected) return ERROR_ACCESS_DENIED;
    }
    return ERROR_SUCCESS;
}

template<class Selection>
inline LSTATUS remove(HKEY root, const Plan& plan,
                      const Selection& selected, bool requireOwner,
                      bool requireAllValues = false) noexcept {
    for (size_t i = 0; i < plan.nodes.size(); ++i) {
        if (!selected[i]) continue;
        LSTATUS status = inspect(root, plan, i, selected, requireOwner,
                                 requireAllValues);
        if (status != ERROR_SUCCESS) return status;
    }
    for (size_t i = plan.nodes.size(); i-- != 0;) {
        if (!selected[i]) continue;
        // Nonrecursive deletion removes this validated key and its values
        // together. A denied deletion retains the ownership marker for retry.
        LSTATUS status = ::RegDeleteKeyW(root, plan.nodes[i].path.c_str());
        if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND &&
            status != ERROR_PATH_NOT_FOUND) return status;
    }
    return ERROR_SUCCESS;
}
} // namespace registration_detail

// root is an explicit borrowed Classes root (e.g. HKCU\Software\Classes).
// No COM initialization, elevation, registry-view switching or existing-key
// overwrite occurs. Caller must exclude concurrent installer/registry writers.
inline HRESULT RegisterClass(HKEY root, const ClassRegistration& entry) noexcept {
    using namespace registration_detail;
    if (!root) return E_INVALIDARG;
    try {
        Plan plan;
        HRESULT hr = plan.Build(entry);
        if (FAILED(hr)) return hr;
        // This shared container is never removed by rollback/unregistration.
        Key classes;
        LSTATUS status = ::RegCreateKeyExW(root, L"CLSID", 0, nullptr, 0,
                                          KEY_READ | KEY_WRITE, nullptr, &classes.value, nullptr);
        if (status != ERROR_SUCCESS) return result(status);
        std::array<bool, 5> created{};
        for (size_t i = 0; i < plan.nodes.size(); ++i) {
            Key key;
            DWORD disposition = 0;
            status = ::RegCreateKeyExW(root, plan.nodes[i].path.c_str(), 0, nullptr, 0,
                                       KEY_READ | KEY_WRITE, nullptr, &key.value, &disposition);
            if (status == ERROR_SUCCESS && disposition != REG_CREATED_NEW_KEY)
                status = ERROR_ALREADY_EXISTS;
            if (status == ERROR_SUCCESS) {
                created[i] = true;
                for (const Value& value : plan.nodes[i].values) {
                    status = ::RegSetValueExW(key.value, value.name.c_str(), 0, REG_SZ,
                        reinterpret_cast<const BYTE*>(value.text.c_str()),
                        static_cast<DWORD>((value.text.size() + 1) * sizeof(wchar_t)));
                    if (status != ERROR_SUCCESS) break;
                }
            }
            if (status != ERROR_SUCCESS) {
                LSTATUS cleanup = remove(root, plan, created, false);
                return result(cleanup != ERROR_SUCCESS ? cleanup : status);
            }
        }
        return S_OK;
    } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
      catch (...) { return E_UNEXPECTED; }
}

inline HRESULT UnregisterClass(HKEY root, const ClassRegistration& entry) noexcept {
    using namespace registration_detail;
    if (!root) return E_INVALIDARG;
    try {
        Plan plan;
        HRESULT hr = plan.Build(entry);
        if (FAILED(hr)) return hr;
        const std::array<bool, 5> selected{{true, true, true, true, true}};
        return result(remove(root, plan, selected, true));
    } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
      catch (...) { return E_UNEXPECTED; }
}

// Multi-class registration composes the same strict single-class
// registrar, preserving existing ownership markers, nonrecursive deletion
// and fail-closed handling of foreign registry content.
//
// This is not a registry transaction. The installer owns exclusive access
// to its supplied Classes root during preflight, writes and rollback.
inline HRESULT ValidateClassBatch(const ClassRegistration* entries,
                                  size_t count) noexcept {
    using namespace registration_detail;
    if (!entries && count) return E_INVALIDARG;
    if (count > 1024) return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
    try {
        // Prevalidate the whole immutable input before a single registry write.
        for (size_t i = 0; i < count; ++i) {
            Plan plan;
            HRESULT hr = plan.Build(entries[i]);
            if (FAILED(hr)) return hr;
            for (size_t j = 0; j < i; ++j) {
                if (IsEqualCLSID(entries[i].clsid, entries[j].clsid))
                    return E_INVALIDARG;
                // ProgID paths are case-insensitive Windows registry keys.
                if (entries[i].progId && entries[j].progId &&
                    ::lstrcmpiW(entries[i].progId, entries[j].progId) == 0)
                    return E_INVALIDARG;
            }
        }
        return S_OK;
    } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
      catch (...) { return E_UNEXPECTED; }
}

// Register 1..1024 classes under a borrowed root. If a later class fails,
// uninstall only classes actually created by this batch, in reverse order.
// Rollback conflicts are surfaced rather than silently ignored.
inline HRESULT RegisterClasses(HKEY root, const ClassRegistration* entries,
                               size_t count) noexcept {
    if (!root) return E_INVALIDARG;
    if (!count) return S_FALSE;
    HRESULT hr = ValidateClassBatch(entries, count);
    if (FAILED(hr)) return hr;
    for (size_t i = 0; i < count; ++i) {
        hr = RegisterClass(root, entries[i]);
        if (FAILED(hr)) {
            HRESULT rollback = S_OK;
            while (i != 0) {
                --i;
                const HRESULT cleanup = UnregisterClass(root, entries[i]);
                if (FAILED(cleanup) && SUCCEEDED(rollback))
                    rollback = cleanup;
            }
            return FAILED(rollback) ? rollback : hr;
        }
    }
    return S_OK;
}

// Check *every* class's expected ownership/values and child key structure
// before removing any class. This avoids partial batch deletion caused by
// a pre-existing foreign value in a later class; external races remain the
// explicit caller's responsibility.
inline HRESULT UnregisterClasses(HKEY root, const ClassRegistration* entries,
                                 size_t count) noexcept {
    using namespace registration_detail;
    if (!root) return E_INVALIDARG;
    if (!count) return S_FALSE;
    HRESULT hr = ValidateClassBatch(entries, count);
    if (FAILED(hr)) return hr;
    try {
        const std::array<bool, 5> selected{{true, true, true, true, true}};
        for (size_t i = 0; i < count; ++i) {
            Plan plan;
            hr = plan.Build(entries[i]);
            if (FAILED(hr)) return hr;
            for (size_t j = 0; j < plan.nodes.size(); ++j) {
                const LSTATUS state = inspect(root, plan, j, selected, true);
                if (state != ERROR_SUCCESS) return result(state);
            }
        }
        for (size_t i = count; i-- != 0;) {
            hr = UnregisterClass(root, entries[i]);
            if (FAILED(hr)) return hr;
        }
        return S_OK;
    } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
      catch (...) { return E_UNEXPECTED; }
}

}} // namespace OpenATL::COM
#endif
