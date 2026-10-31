// OpenATL - owner-verified execution of bounded ATL RGS documents.
// A caller supplies an OPENED, writable Classes-root HKEY. Symbolic HKCR
// in the source document NEVER selects HKCR/HKLM or an implicit elevation.
// This uses the SAME registration_detail::Plan/inspect/remove machinery as
// the established ClassRegistration registrar.
#ifndef OPENATL_RGS_REGISTRATION_H
#define OPENATL_RGS_REGISTRATION_H
#include "com_registration.h"
#include "rgs_parser.h"

#include <string_view>
#include <vector>

namespace OpenATL { namespace COM {

namespace rgs_registration_detail {
using namespace registration_detail;
struct Prepared {
    Plan plan;
    std::vector<bool> owned; // NoRemove CLSID is a shared container.
};
#ifdef OPENATL_RGS_TEST_FAULT
// Native regression-only hook: simulate the next RegSetValueEx failure
// after this owned key has been newly created, to test bounded rollback.
inline int& FailedWriteAtNode() noexcept {
    static int node = -1;
    return node;
}
#endif

// This API intentionally rejects predefined root constants. A handle
// returned by RegCreateKeyExW/RegOpenKeyExW is required. The caller remains
// responsible for obtaining it from the intended HKCU/isolated root.
inline bool OpenedRoot(HKEY root) noexcept {
    return root && root != HKEY_CLASSES_ROOT && root != HKEY_LOCAL_MACHINE &&
           root != HKEY_CURRENT_USER && root != HKEY_USERS &&
           root != HKEY_CURRENT_CONFIG && root != HKEY_PERFORMANCE_DATA;
}
inline bool OwnerTag(std::wstring_view owner) noexcept {
    if (owner.empty() || owner.size() > 64) return false;
    for (wchar_t c : owner)
        if (!((c >= L'A' && c <= L'Z') ||
              (c >= L'a' && c <= L'z') ||
              (c >= L'0' && c <= L'9') ||
              c == L'_' || c == L'-' || c == L'.')) return false;
    return true;
}
inline bool AbsoluteModule(std::wstring_view value) noexcept {
    if (value.empty() || value.size() > RGS::MaxPathUnits ||
        value.find(L'\0') != value.npos || value.find(L'%') != value.npos ||
        value.find(L'"') != value.npos)
        return false;
    bool drive = value.size() >= 3 &&
        ((value[0] >= L'A' && value[0] <= L'Z') ||
         (value[0] >= L'a' && value[0] <= L'z')) &&
        value[1] == L':' && value[2] == L'\\';
    bool unc = value.size() >= 5 && value[0] == L'\\' &&
               value[1] == L'\\' && value[2] != L'\\';
    return drive || unc;
}
inline HRESULT CanonicalModule(std::wstring_view raw, std::wstring& canonical) {
    if (!AbsoluteModule(raw)) return E_INVALIDARG;
    const std::wstring input(raw);
    const DWORD capacity = ::GetFullPathNameW(input.c_str(), 0, nullptr, nullptr);
    if (!capacity) return result(::GetLastError());
    if (capacity > RGS::MaxPathUnits + 1) return E_INVALIDARG;
    std::wstring buffer(capacity, L'\0');
    const DWORD length = ::GetFullPathNameW(input.c_str(), capacity,
                                            &buffer[0], nullptr);
    if (!length) return result(::GetLastError());
    if (length >= capacity) return result(ERROR_INSUFFICIENT_BUFFER);
    buffer.resize(length);
    if (!AbsoluteModule(buffer)) return E_INVALIDARG;
    canonical = std::move(buffer);
    return S_OK;
}
inline HRESULT MapError(RGS::Error error) noexcept {
    switch (error) {
    case RGS::Error::Ok: return S_OK;
    case RGS::Error::Limit:
        return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
    case RGS::Error::AllocationFailure: return E_OUTOFMEMORY;
    case RGS::Error::Syntax:
    case RGS::Error::Duplicate:
    case RGS::Error::InvalidName: return E_INVALIDARG;
    default: return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    }
}
inline bool ValidPath(std::wstring_view path) noexcept {
    if (path.empty() || path.size() > RGS::MaxPathUnits) return false;
    size_t start = 0, depth = 0;
    while (start < path.size()) {
        if (++depth > RGS::MaxNesting + 1) return false;
        const size_t end = path.find(L'\\', start);
        const size_t length = (end == path.npos ? path.size() : end) - start;
        if (length == 0 ||
            !RGS::detail::Name(path.substr(start, length))) return false;
        if (end == path.npos) return true;
        start = end + 1;
    }
    return false; // Trailing separator.
}
inline bool HasParentBefore(const RGS::Document& doc, size_t i) noexcept {
    const auto& path = doc.keys[i].path;
    const size_t pos = path.rfind(L'\\');
    if (pos == path.npos) return true;
    const std::wstring_view parent(path.data(), pos);
    for (size_t j = 0; j < i; ++j)
        if (RGS::detail::Same(doc.keys[j].path, parent)) return true;
    return false;
}
inline HRESULT Prepare(const RGS::Document& source,
                       std::wstring_view module, std::wstring_view owner,
                       Prepared& prepared, bool quoteModule = false) {
    if (!OwnerTag(owner) || source.keys.empty() ||
        source.keys.size() > RGS::MaxKeys) return E_INVALIDARG;
    std::wstring canonical;
    HRESULT hr = CanonicalModule(module, canonical);
    if (FAILED(hr)) return hr;
    RGS::Document expanded;
    const RGS::Error error = RGS::ResolveModule(source, canonical, expanded, quoteModule);
    if (error != RGS::Error::Ok) return MapError(error);
    const std::wstring marker = std::wstring(L"RGS1:") +
                                std::wstring(owner) + L":" + canonical;
    size_t totalValues = 0;
    Prepared staged;
    staged.plan.nodes.reserve(expanded.keys.size());
    staged.owned.reserve(expanded.keys.size());
    for (size_t i = 0; i < expanded.keys.size(); ++i) {
        const auto& key = expanded.keys[i];
        if (!ValidPath(key.path) || !HasParentBefore(expanded, i))
            return E_INVALIDARG;
        for (size_t j = 0; j < i; ++j)
            if (RGS::detail::Same(key.path, expanded.keys[j].path))
                return E_INVALIDARG;
        // Shared NoRemove containers are *not owned*. Only the top-level
        // CLSID grouping can be shared, with no values of its own.
        const bool shared = key.directive == RGS::Directive::NoRemove;
        if (shared && (!RGS::detail::Same(key.path, L"CLSID") ||
                       !key.values.empty())) return E_INVALIDARG;
        if (!shared && key.directive != RGS::Directive::Ordinary &&
            key.directive != RGS::Directive::ForceRemove)
            return E_INVALIDARG;
        if (totalValues + key.values.size() > RGS::MaxValues)
            return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
        totalValues += key.values.size();
        Node target;
        target.path = key.path;
        if (!shared) target.values.push_back({L"OpenATL.COMOwner", marker});
        for (size_t j = 0; j < key.values.size(); ++j) {
            const auto& value = key.values[j];
            if ((!value.name.empty() && !RGS::detail::Name(value.name)) ||
                value.text.size() > RGS::MaxValueUnits ||
                value.text.find(L'\0') != value.text.npos ||
                value.text.find(L'%') != value.text.npos)
                return E_INVALIDARG;
            for (size_t k = 0; k < j; ++k)
                if (RGS::detail::Same(value.name, key.values[k].name))
                    return E_INVALIDARG;
            target.values.push_back({value.name, value.text});
        }
        staged.plan.nodes.push_back(std::move(target));
        staged.owned.push_back(!shared);
    }
    prepared = std::move(staged);
    return S_OK;
}
inline HRESULT PreflightNew(HKEY root, const Prepared& plan) noexcept {
    for (size_t i = 0; i < plan.plan.nodes.size(); ++i) {
        if (!plan.owned[i]) continue;
        Key key;
        const LSTATUS status = ::RegOpenKeyExW(root,
            plan.plan.nodes[i].path.c_str(), 0, KEY_QUERY_VALUE, &key.value);
        if (status == ERROR_SUCCESS) return result(ERROR_ALREADY_EXISTS);
        if (status != ERROR_FILE_NOT_FOUND && status != ERROR_PATH_NOT_FOUND)
            return result(status);
    }
    return S_OK;
}
} // namespace rgs_registration_detail

// Registers only new owned keys. ForceRemove is NOT an overwrite or
// RegDeleteTree directive. A failure triggers verification and reverse
// deletion of new keys only; foreign changes fail closed even during rollback.
// Caller must serialize all writers to the explicit Classes root.
inline HRESULT RegisterRgs(HKEY classesRoot, const RGS::Document& document,
                           std::wstring_view modulePath,
                           std::wstring_view ownerTag, bool quoteModule = false) noexcept {
    using namespace rgs_registration_detail;
    if (!OpenedRoot(classesRoot)) return E_INVALIDARG;
    try {
        Prepared plan;
        HRESULT hr = Prepare(document, modulePath, ownerTag, plan, quoteModule);
        if (FAILED(hr)) return hr;
        hr = PreflightNew(classesRoot, plan);
        if (FAILED(hr)) return hr;
        std::vector<bool> created(plan.plan.nodes.size(), false);
        for (size_t i = 0; i < plan.plan.nodes.size(); ++i) {
            const Node& node = plan.plan.nodes[i];
            Key key;
            DWORD disposition = 0;
            LSTATUS status = ::RegCreateKeyExW(
                classesRoot, node.path.c_str(), 0, nullptr, 0,
                KEY_READ | KEY_WRITE, nullptr, &key.value, &disposition);
            if (status == ERROR_SUCCESS && plan.owned[i]) {
                if (disposition != REG_CREATED_NEW_KEY) {
                    status = ERROR_ALREADY_EXISTS;
                } else {
                    created[i] = true; // Before writing owner for partial-failure cleanup.
                    for (const auto& value : node.values) {
#ifdef OPENATL_RGS_TEST_FAULT
                        if (rgs_registration_detail::FailedWriteAtNode() ==
                            static_cast<int>(i)) {
                            status = ERROR_WRITE_FAULT;
                            break;
                        }
#endif
                        status = ::RegSetValueExW(key.value, value.name.c_str(),
                            0, REG_SZ,
                            reinterpret_cast<const BYTE*>(value.text.c_str()),
                            static_cast<DWORD>((value.text.size() + 1) * sizeof(wchar_t)));
                        if (status != ERROR_SUCCESS) break;
                    }
                }
            }
            if (status != ERROR_SUCCESS) {
                // Close the failed/partially-created key before attempting
                // nonrecursive RegDeleteKeyW on its parent.
                if (key.value) { ::RegCloseKey(key.value); key.value = nullptr; }
                const LSTATUS cleanup = registration_detail::remove(
                    classesRoot, plan.plan, created, false);
                return registration_detail::result(
                    cleanup != ERROR_SUCCESS ? cleanup : status);
            }
        }
        return S_OK;
    } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
      catch (...) { return E_UNEXPECTED; }
}

// Before deleting ANY owned RGS key, verify the entire expected owner/value
// and direct-child set. Shared NoRemove CLSID remains untouched, even if
// this installation was the operation that first created it.
inline HRESULT UnregisterRgs(HKEY classesRoot, const RGS::Document& document,
                             std::wstring_view modulePath,
                             std::wstring_view ownerTag, bool quoteModule = false) noexcept {
    using namespace rgs_registration_detail;
    if (!OpenedRoot(classesRoot)) return E_INVALIDARG;
    try {
        Prepared plan;
        const HRESULT hr = Prepare(document, modulePath, ownerTag, plan, quoteModule);
        if (FAILED(hr)) return hr;
        return registration_detail::result(registration_detail::remove(
            classesRoot, plan.plan, plan.owned, true, true));
    } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
      catch (...) { return E_UNEXPECTED; }
}

}} // namespace OpenATL::COM
#endif // OPENATL_RGS_REGISTRATION_H
