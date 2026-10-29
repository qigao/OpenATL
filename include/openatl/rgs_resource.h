// OpenATL - real ATL REGISTRY resource loading and scoped installation.
//
// DECLARE_REGISTRY_RESOURCEID uses UpdateRegistryResource() only while an
// application-supplied ScopedRgsRegistrationContext is alive on the invoking
// thread and in the SAME DLL image. This module never chooses HKCR/HKLM,
// opens a default key, starts COM, or executes registration in DllMain.
#ifndef OPENATL_RGS_RESOURCE_H
#define OPENATL_RGS_RESOURCE_H

#include "rgs_registration.h"
#include <cstdint>
#include <exception>
#include <string>
#include <string_view>
#include <utility>

namespace OpenATL { namespace COM {

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

// Resource loader (no registry I/O): the caller specifies both HMODULE and
// integer resource ID, and the resource type must be literal "REGISTRY".
// A failed load/parse does not mutate the supplied output document.
inline HRESULT ReadRgsResource(HMODULE image, unsigned resourceId,
                               RGS::Document& document) noexcept {
    if (!image || !resourceId || resourceId > 0xffff)
        return E_INVALIDARG;
    const HRSRC resource = ::FindResourceW(
        image, MAKEINTRESOURCEW(resourceId), L"REGISTRY");
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
    const RGS::Error error = RGS::Parse(wide, parsed);
    if (error != RGS::Error::Ok)
        return rgs_registration_detail::MapError(error);
    try {
        document = std::move(parsed);
        return S_OK;
    } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
      catch (...) { return E_UNEXPECTED; }
}

// Called by DECLARE_REGISTRY_RESOURCEID on an ATL class. Context is
// mandatory; a standalone COM factory lookup or plain WTL window never
// accidentally inherits registry policy or initializes COM.
inline HRESULT UpdateRegistryResource(unsigned resourceId,
                                      BOOL registerClass) noexcept {
    const RgsRegistrationContext* context = rgs_resource_detail::Current();
    if (!context) return E_UNEXPECTED;
    RGS::Document document;
    HRESULT hr = ReadRgsResource(context->resourceModule, resourceId, document);
    if (FAILED(hr)) return hr;
    return registerClass
        ? RegisterRgs(context->classesRoot, document,
                      context->modulePath, context->ownerTag)
        : UnregisterRgs(context->classesRoot, document,
                        context->modulePath, context->ownerTag);
}
}} // namespace OpenATL::COM
#endif // OPENATL_RGS_RESOURCE_H
