// OpenATL - real ATL REGISTRY resource loading and scoped installation.
//
// Explicit contexts remain supported. DLL module registration also supplies
// a lazy image-local context selected by its captured per-user setting.
// Neither inclusion nor DllMain opens registry keys or initializes COM.
#ifndef OPENATL_RGS_RESOURCE_H
#define OPENATL_RGS_RESOURCE_H

#include "rgs_registration.h"
#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace OpenATL { namespace COM {

class ScopedModuleRgsRegistration;

struct RgsRegistrationContext {
    HKEY classesRoot = nullptr;         // Borrowed opened Classes-root handle.
    HMODULE resourceModule = nullptr;   // Borrowed image handle, must remain loaded.
    std::wstring modulePath;            // Owning canonicalized absolute DLL path.
    std::wstring ownerTag;              // Owning scoped registration identity.
};

namespace rgs_resource_detail {
inline const RgsRegistrationContext*& Current() noexcept {
    static thread_local const RgsRegistrationContext* context = nullptr;
    return context;
}
inline ScopedModuleRgsRegistration*& CurrentModule() noexcept {
    static thread_local ScopedModuleRgsRegistration* module = nullptr;
    return module;
}
inline HRESULT Win32Failure(DWORD fallback) noexcept {
    const DWORD error = ::GetLastError();
    return HRESULT_FROM_WIN32(error ? error : fallback);
}
inline HRESULT Decode(const BYTE* bytes, size_t size,
                      std::wstring& output) noexcept {
    if (!bytes || !size || size > RGS::MaxSourceUnits * 4 + 4)
        return E_INVALIDARG;
    try {
        std::wstring decoded;
        if (size >= 2 && ((bytes[0] == 0xff && bytes[1] == 0xfe) ||
                          (bytes[0] == 0xfe && bytes[1] == 0xff))) {
            if ((size - 2) % 2 || size == 2)
                return HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION);
            const bool little = bytes[0] == 0xff;
            decoded.reserve((size - 2) / 2);
            for (size_t i = 2; i < size; i += 2) {
                uint16_t unit = little ?
                    uint16_t(bytes[i] | (unsigned(bytes[i + 1]) << 8)) :
                    uint16_t((unsigned(bytes[i]) << 8) | bytes[i + 1]);
                if (unit == 0)
                    return HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION);
                if (unit >= 0xd800 && unit <= 0xdbff) {
                    if (i + 3 >= size)
                        return HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION);
                    uint16_t low = little ?
                        uint16_t(bytes[i + 2] | (unsigned(bytes[i + 3]) << 8)) :
                        uint16_t((unsigned(bytes[i + 2]) << 8) | bytes[i + 3]);
                    if (low < 0xdc00 || low > 0xdfff)
                        return HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION);
                    decoded.push_back(static_cast<wchar_t>(unit));
                    decoded.push_back(static_cast<wchar_t>(low));
                    i += 2;
                } else if (unit >= 0xdc00 && unit <= 0xdfff) {
                    return HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION);
                } else {
                    decoded.push_back(static_cast<wchar_t>(unit));
                }
                if (decoded.size() > RGS::MaxSourceUnits)
                    return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
            }
        } else {
            // Plain files and UTF-8 BOM files share the strict UTF-8 decoder.
            // MB_ERR_INVALID_CHARS never silently replaces malformed bytes.
            const bool bom = size >= 3 && bytes[0] == 0xef &&
                             bytes[1] == 0xbb && bytes[2] == 0xbf;
            const BYTE* input = bytes + (bom ? 3 : 0);
            const size_t count = size - (bom ? 3 : 0);
            if (count == 0 || count > RGS::MaxSourceUnits * 4)
                return E_INVALIDARG;
            for (size_t i = 0; i < count; ++i)
                if (input[i] == 0)
                    return HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION);
            const int needed = ::MultiByteToWideChar(
                CP_UTF8, MB_ERR_INVALID_CHARS,
                reinterpret_cast<const char*>(input), static_cast<int>(count),
                nullptr, 0);
            if (needed <= 0)
                return HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION);
            if (static_cast<size_t>(needed) > RGS::MaxSourceUnits)
                return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
            decoded.resize(needed);
            if (::MultiByteToWideChar(
                    CP_UTF8, MB_ERR_INVALID_CHARS,
                    reinterpret_cast<const char*>(input), static_cast<int>(count),
                    &decoded[0], needed) != needed)
                return HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION);
        }
        if (decoded.empty() || decoded.size() > RGS::MaxSourceUnits)
            return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
        output = std::move(decoded);
        return S_OK;
    } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
      catch (...) { return E_UNEXPECTED; }
}
} // namespace rgs_resource_detail

// Scoped to a single synchronous installer invocation on the same module's
// thread. Nested scopes are rejected (no provider fallback); no raw state
// escapes the RAII lifetime. Caller must serialize registry writes separately.
class ScopedRgsRegistrationContext final {
public:
    ScopedRgsRegistrationContext(HKEY openedClassesRoot,
                                 HMODULE resourceModule,
                                 std::wstring_view ownerTag) noexcept {
        if (rgs_resource_detail::Current()) {
            status_ = HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
            return;
        }
        if (!rgs_registration_detail::OpenedRoot(openedClassesRoot) ||
            !resourceModule ||
            !rgs_registration_detail::OwnerTag(ownerTag)) {
            status_ = E_INVALIDARG;
            return;
        }
        try {
            wchar_t path[RGS::MaxPathUnits + 2]{};
            const DWORD length = ::GetModuleFileNameW(
                resourceModule, path, static_cast<DWORD>(
                    sizeof(path) / sizeof(path[0])));
            if (!length) {
                status_ = rgs_resource_detail::Win32Failure(ERROR_INVALID_HANDLE);
                return;
            }
            if (length >= sizeof(path) / sizeof(path[0])) {
                status_ = HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
                return;
            }
            std::wstring canonical;
            status_ = rgs_registration_detail::CanonicalModule(
                std::wstring_view(path, length), canonical);
            if (FAILED(status_)) return;
            context_.classesRoot = openedClassesRoot;
            context_.resourceModule = resourceModule;
            context_.modulePath = std::move(canonical);
            context_.ownerTag.assign(ownerTag);
            rgs_resource_detail::Current() = &context_;
            armed_ = true;
            status_ = S_OK;
        } catch (const std::bad_alloc&) { status_ = E_OUTOFMEMORY; }
          catch (...) { status_ = E_UNEXPECTED; }
    }
    ~ScopedRgsRegistrationContext() noexcept {
        if (!armed_) return;
        if (rgs_resource_detail::Current() != &context_)
            std::terminate(); // A scoped DLL callback cannot outlive its owner.
        rgs_resource_detail::Current() = nullptr;
    }
    ScopedRgsRegistrationContext(const ScopedRgsRegistrationContext&) = delete;
    ScopedRgsRegistrationContext& operator=(const ScopedRgsRegistrationContext&) = delete;
    HRESULT Status() const noexcept { return status_; }
private:
    RgsRegistrationContext context_;
    HRESULT status_ = E_UNEXPECTED;
    bool armed_ = false;
};

// DLL-module operation scope. Root access is deferred until an actual RGS
// callback, so custom/no-registry callbacks do not acquire registry rights.
// The first RGS callback retains one opened root and context through all
// later callbacks, TypeLib work and rollback in this operation.
class ScopedModuleRgsRegistration final {
public:
    ScopedModuleRgsRegistration(HMODULE image, bool perUser) noexcept
        : image_(image), perUser_(perUser), previous_(rgs_resource_detail::CurrentModule()) {
        rgs_resource_detail::CurrentModule() = this;
    }
    ~ScopedModuleRgsRegistration() noexcept {
        context_.reset(); // Release TLS context before closing its root handle.
        if (rgs_resource_detail::CurrentModule() != this) std::terminate();
        rgs_resource_detail::CurrentModule() = previous_;
    }
    ScopedModuleRgsRegistration(const ScopedModuleRgsRegistration&) = delete;
    ScopedModuleRgsRegistration& operator=(const ScopedModuleRgsRegistration&) = delete;
    HMODULE ResourceModule() const noexcept { return image_; }

    HRESULT Ensure() noexcept {
        if (rgs_resource_detail::Current()) return S_OK; // Explicit scope wins.
        if (attempted_) return status_;
        attempted_ = true;
        if (!image_) return status_ = E_INVALIDARG;
        if (perUser_) {
            registration_detail::Key user;
            LSTATUS state = ::RegOpenCurrentUser(KEY_READ | KEY_WRITE, &user.value);
            if (state != ERROR_SUCCESS) return status_ = HRESULT_FROM_WIN32(state);
            state = ::RegCreateKeyExW(user.value, L"Software\\Classes", 0,
                nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &root_.value, nullptr);
            if (state != ERROR_SUCCESS) return status_ = HRESULT_FROM_WIN32(state);
        } else {
            // Get a real opened merged Classes handle, not the predefined
            // HKCR pseudo-handle rejected by the owned-root registrar.
            HANDLE token = nullptr;
            if (!::OpenThreadToken(::GetCurrentThread(), TOKEN_QUERY, TRUE, &token)) {
                const DWORD error = ::GetLastError();
                if (error != ERROR_NO_TOKEN) return status_ = HRESULT_FROM_WIN32(error);
                if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token))
                    return status_ = rgs_resource_detail::Win32Failure(ERROR_ACCESS_DENIED);
            }
            const LSTATUS state = ::RegOpenUserClassesRoot(token, 0,
                KEY_READ | KEY_WRITE, &root_.value);
            ::CloseHandle(token);
            if (state != ERROR_SUCCESS) return status_ = HRESULT_FROM_WIN32(state);
        }
        // The existing ownership marker also contains the canonical image
        // path, so another DLL cannot adopt these keys using this tag alone.
        context_.emplace(root_.value, image_, L"OpenATL.Module");
        return status_ = context_->Status();
    }
private:
    HMODULE image_;
    const bool perUser_;
    ScopedModuleRgsRegistration* previous_;
    bool attempted_ = false;
    HRESULT status_ = E_UNEXPECTED;
    registration_detail::Key root_;
    std::optional<ScopedRgsRegistrationContext> context_;
};

// Resource loader (no registry I/O): the caller specifies both HMODULE and
// resource ID/name, and the resource type must be literal "REGISTRY".
// A failed load/parse does not mutate the supplied output document.
inline HRESULT ReadRgsResource(HMODULE image, LPCWSTR resourceName,
                               RGS::Document& document,
                               const std::vector<RGS::Replacement>* map = nullptr) noexcept {
    if (!image || !resourceName ||
        (!IS_INTRESOURCE(resourceName) &&
         (!registration_detail::bounded(resourceName, RGS::MaxNameUnits) ||
          !*resourceName)))
        return E_INVALIDARG;
    if (map) {
        const HRESULT hr = rgs_registration_detail::MapError(
            RGS::ValidateReplacements(*map));
        if (FAILED(hr)) return hr;
    }
    const HRSRC resource = ::FindResourceW(
        image, resourceName, L"REGISTRY");
    if (!resource)
        return rgs_resource_detail::Win32Failure(ERROR_RESOURCE_NAME_NOT_FOUND);
    const DWORD size = ::SizeofResource(image, resource);
    if (!size)
        return rgs_resource_detail::Win32Failure(ERROR_RESOURCE_DATA_NOT_FOUND);
    const HGLOBAL loaded = ::LoadResource(image, resource);
    if (!loaded)
        return rgs_resource_detail::Win32Failure(ERROR_RESOURCE_DATA_NOT_FOUND);
    const auto* bytes = static_cast<const BYTE*>(::LockResource(loaded));
    if (!bytes)
        return HRESULT_FROM_WIN32(ERROR_RESOURCE_DATA_NOT_FOUND);
    std::wstring wide;
    HRESULT hr = rgs_resource_detail::Decode(bytes, size, wide);
    if (FAILED(hr)) return hr;
    RGS::Document parsed;
    RGS::Parser parser;
    const RGS::Error error = parser.Parse(wide, parsed, map);
    if (error != RGS::Error::Ok)
        return rgs_registration_detail::MapError(error);
    try {
        document = std::move(parsed);
        return S_OK;
    } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
      catch (...) { return E_UNEXPECTED; }
}
inline HRESULT ReadRgsResource(HMODULE image, unsigned resourceId,
                               RGS::Document& document,
                               const std::vector<RGS::Replacement>* map = nullptr) noexcept {
    if (!resourceId || resourceId > 0xffff) return E_INVALIDARG;
    return ReadRgsResource(image, MAKEINTRESOURCEW(resourceId), document, map);
}

// Called by DECLARE_REGISTRY_RESOURCEID on an ATL class. A module supplies
// the lazy default during registration; isolated callers can supply an
// explicit context. Plain WTL/factory use creates neither policy nor COM.
inline HRESULT ApplyRgsDocument(const RGS::Document& document, BOOL registerClass) noexcept {
    const RgsRegistrationContext* context = rgs_resource_detail::Current();
    if (!context) {
        auto* module = rgs_resource_detail::CurrentModule();
        if (!module) return E_UNEXPECTED;
        const HRESULT hr = module->Ensure();
        if (FAILED(hr)) return hr;
        context = rgs_resource_detail::Current();
    }
    if (!context) return E_UNEXPECTED;
    return registerClass
        ? RegisterRgs(context->classesRoot, document, context->modulePath, context->ownerTag)
        : UnregisterRgs(context->classesRoot, document, context->modulePath, context->ownerTag);
}
inline HRESULT UpdateRegistryResource(LPCWSTR resourceName, BOOL registerClass,
                     const std::vector<RGS::Replacement>* map = nullptr) noexcept {
    const RgsRegistrationContext* context = rgs_resource_detail::Current();
    auto* module = rgs_resource_detail::CurrentModule();
    if (!context && !module) return E_UNEXPECTED;
    // Decode, expand and parse before acquiring the default registry root.
    RGS::Document document;
    HRESULT hr = ReadRgsResource(context ? context->resourceModule :
                                module->ResourceModule(), resourceName, document, map);
    if (FAILED(hr)) return hr;
    return ApplyRgsDocument(document, registerClass);
}
inline HRESULT UpdateRegistryResource(unsigned resourceId, BOOL registerClass,
                     const std::vector<RGS::Replacement>* map = nullptr) noexcept {
    if (!resourceId || resourceId > 0xffff) return E_INVALIDARG;
    return UpdateRegistryResource(MAKEINTRESOURCEW(resourceId), registerClass, map);
}
}} // namespace OpenATL::COM
#endif // OPENATL_RGS_RESOURCE_H
