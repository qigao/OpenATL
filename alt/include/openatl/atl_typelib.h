// ATL TypeLib helpers over Windows OLEAUT32. Included by atlbase.h after
// the core module and owning COM wrappers have been declared.
#ifndef OPENATL_ATL_TYPELIB_H
#define OPENATL_ATL_TYPELIB_H
#include <atomic>
#include <cstring>
#include <oleauto.h>

namespace ATL {
namespace detail {
// Configure before registration starts. The setting belongs to this image;
// it does not redirect arbitrary Win32 registry calls or another DLL's ATL.
inline std::atomic<bool> perUserRegistration{false};
}

inline HRESULT WINAPI AtlSetPerUserRegistration(bool enabled) noexcept {
    detail::perUserRegistration.store(enabled);
    return S_OK;
}
inline HRESULT WINAPI AtlGetPerUserRegistration(bool* enabled) noexcept {
    if (!enabled) return E_POINTER;
    *enabled = detail::perUserRegistration.load();
    return S_OK;
}

inline HRESULT WINAPI AtlLoadTypeLib(HINSTANCE image, LPCOLESTR index,
                                     BSTR* outputPath, ITypeLib** output) noexcept {
    if (outputPath) *outputPath = nullptr;
    if (output) *output = nullptr;
    if (!outputPath || !output) return E_POINTER;
    if (!image) return E_INVALIDARG;
    size_t suffix = 0;
    if (index && *index) {
        if (index[0] != L'\\') return E_INVALIDARG;
        unsigned value = 0;
        for (suffix = 1; suffix <= 5 && index[suffix]; ++suffix) {
            if (index[suffix] < L'0' || index[suffix] > L'9') return E_INVALIDARG;
            value = value * 10 + unsigned(index[suffix] - L'0');
        }
        if (index[suffix] || !value || value > 65535) return E_INVALIDARG;
    }
    wchar_t path[8193]{};
    const DWORD length = ::GetModuleFileNameW(image, path, 8193);
    if (!length) return HRESULT_FROM_WIN32(::GetLastError());
    if (length >= 8193 || size_t(length) + suffix > 8192)
        return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
    if (suffix) std::memcpy(path + length, index, (suffix + 1) * sizeof(wchar_t));
    CComPtr<ITypeLib> library;
    HRESULT hr = ::LoadTypeLibEx(path, REGKIND_NONE, &library.p);
    if (FAILED(hr)) return hr;
    if (!library.p) return E_UNEXPECTED;
    BSTR ownedPath = ::SysAllocStringLen(path, length + static_cast<UINT>(suffix));
    if (!ownedPath) return E_OUTOFMEMORY;
    *outputPath = ownedPath;
    *output = library.Detach();
    return S_OK;
}

namespace detail {
class TypeLibRegistration final {
public:
    explicit TypeLibRegistration(bool perUser) noexcept : perUser_(perUser) {}
    HRESULT Prepare(HINSTANCE image, LPCOLESTR index = nullptr) noexcept {
        HRESULT hr = AtlLoadTypeLib(image, index, &path_.m_str, &library_.p);
        if (FAILED(hr)) return hr;
        TLIBATTR* attr = nullptr;
        hr = library_->GetLibAttr(&attr);
        if (FAILED(hr)) return hr;
        if (!attr) return E_UNEXPECTED;
        attributes_ = *attr;
        library_->ReleaseTLibAttr(attr);
        return S_OK;
    }
    HRESULT Register() const noexcept {
        if (!perUser_) return ::RegisterTypeLib(library_.p, path_.m_str, nullptr);
        // Some mingw-w64 headers/import libraries omit these Windows exports.
        // Bind the documented OLEAUT32 ABI directly on both toolchains.
        using Function = HRESULT (WINAPI*)(ITypeLib*, OLECHAR*, OLECHAR*);
        HMODULE module = ::GetModuleHandleW(L"oleaut32.dll");
        if (!module) return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
        const auto function = reinterpret_cast<Function>(
            ::GetProcAddress(module, "RegisterTypeLibForUser"));
        return function ? function(library_.p, path_.m_str, nullptr)
                        : HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }
    HRESULT Unregister() const noexcept {
        const auto& a = attributes_;
        if (!perUser_) return ::UnRegisterTypeLib(a.guid, a.wMajorVerNum,
                                a.wMinorVerNum, a.lcid, a.syskind);
        using Function = HRESULT (WINAPI*)(REFGUID, WORD, WORD, LCID, SYSKIND);
        HMODULE module = ::GetModuleHandleW(L"oleaut32.dll");
        if (!module) return HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND);
        const auto function = reinterpret_cast<Function>(
            ::GetProcAddress(module, "UnRegisterTypeLibForUser"));
        return function ? function(a.guid, a.wMajorVerNum, a.wMinorVerNum, a.lcid, a.syskind)
                        : HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }
private:
    const bool perUser_;
    CComBSTR path_;
    CComPtr<ITypeLib> library_;
    TLIBATTR attributes_{};
};
}

inline HRESULT WINAPI AtlRegisterTypeLib(HINSTANCE image, LPCOLESTR index) noexcept {
    detail::TypeLibRegistration registration(detail::perUserRegistration.load());
    HRESULT hr = registration.Prepare(image, index);
    return FAILED(hr) ? hr : registration.Register();
}
inline HRESULT WINAPI AtlUnRegisterTypeLib(HINSTANCE image, LPCOLESTR index) noexcept {
    detail::TypeLibRegistration registration(detail::perUserRegistration.load());
    HRESULT hr = registration.Prepare(image, index);
    return FAILED(hr) ? hr : registration.Unregister();
}
} // namespace ATL
#endif
