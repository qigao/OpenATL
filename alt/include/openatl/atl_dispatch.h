// OpenATL - true Automation type-info dispatch over Windows OLEAUT32.
//
// Real LoadRegTypeLib / ITypeInfo behavior. No handwritten DISPIDs, no
// alternate dispatch engine and no implicit type-library registration.
// Each query owns and releases its temporary type-library/type-info handles;
// static holder state does not retain an unloaded DLL's ITypeInfo.
#ifndef OPENATL_ATL_DISPATCH_H
#define OPENATL_ATL_DISPATCH_H

#include "../atlcomcli.h"
#include <oaidl.h>
#include <oleauto.h>
#include <type_traits>
#include <new>
#include <string>
#include <string_view>

namespace ATL {

class CComTypeInfoHolder {
public:

    // Load a caller-specified *actual* .tlb/typelib file without registering
    // anything. Unlike LoadTypeLib or REGKIND_DEFAULT, REGKIND_NONE must not
    // mutate HKCR/HKLM/HKCU. Verify library identity/version before exposing
    // a borrowed type library's owned ITypeInfo pointer.
    HRESULT GetTIFromFile(std::wstring_view path, const IID* iid,
                          const GUID* libid, WORD major, WORD minor,
                          ITypeInfo** output) const noexcept {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (!iid || !libid || ::IsEqualGUID(*iid, GUID_NULL) ||
            ::IsEqualGUID(*libid, GUID_NULL) ||
            major == 0xffff || minor == 0xffff || path.empty() ||
            path.size() > 8192 || path.find(L'\0') != path.npos)
            return E_INVALIDARG;
        const bool drive = path.size() >= 3 &&
            ((path[0] >= L'A' && path[0] <= L'Z') ||
             (path[0] >= L'a' && path[0] <= L'z')) &&
            path[1] == L':' && (path[2] == L'\\' || path[2] == L'/');
        const bool unc = path.size() >= 5 && path[0] == L'\\' &&
            path[1] == L'\\' && path[2] != L'\\';
        if (!drive && !unc) return E_INVALIDARG;
        try {
            const std::wstring requested(path);
            const DWORD needed = ::GetFullPathNameW(requested.c_str(), 0,
                                                    nullptr, nullptr);
            if (!needed) return HRESULT_FROM_WIN32(::GetLastError());
            if (needed > 8193) return E_INVALIDARG;
            std::wstring normalized(needed, L'\0');
            const DWORD length = ::GetFullPathNameW(
                requested.c_str(), needed, &normalized[0], nullptr);
            if (!length) return HRESULT_FROM_WIN32(::GetLastError());
            if (length >= needed) return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
            normalized.resize(length);
            ATL::CComPtr<ITypeLib> library;
            HRESULT hr = ::LoadTypeLibEx(normalized.c_str(),
                                         REGKIND_NONE, &library.p);
            if (FAILED(hr)) return hr;
            if (!library.p) return E_UNEXPECTED;
            TLIBATTR* attr = nullptr;
            hr = library->GetLibAttr(&attr);
            if (FAILED(hr)) return hr;
            if (!attr) return E_UNEXPECTED;
            const bool correct = ::IsEqualGUID(attr->guid, *libid) &&
                attr->wMajorVerNum == major && attr->wMinorVerNum == minor;
            library->ReleaseTLibAttr(attr);
            if (!correct) return TYPE_E_ELEMENTNOTFOUND;
            hr = library->GetTypeInfoOfGuid(*iid, output);
            if (SUCCEEDED(hr) && !*output) return E_UNEXPECTED;
            if (FAILED(hr)) *output = nullptr;
            return hr;
        } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
          catch (...) { return E_UNEXPECTED; }
    }

    HRESULT GetTI(const IID* iid, const GUID* libid, WORD major, WORD minor,
                  LCID locale, ITypeInfo** output) const noexcept {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (!iid || !libid || IsEqualGUID(*iid, GUID_NULL) ||
            IsEqualGUID(*libid, GUID_NULL)) return E_INVALIDARG;
        if (major == 0xffff || minor == 0xffff) {
            // ATL's special image-based type-library mode requires an
            // explicit live image+module owner. Do not silently use the
            // registry provider, or pretend image mode already works.
            return major == 0xffff && minor == 0xffff
                ? E_NOTIMPL : E_INVALIDARG;
        }
        CComPtr<ITypeLib> library;
        HRESULT hr = ::LoadRegTypeLib(*libid, major, minor, locale,
                                       &library.p);
        if (FAILED(hr)) return hr;
        if (!library.p) return E_UNEXPECTED;
        hr = library->GetTypeInfoOfGuid(*iid, output);
        if (SUCCEEDED(hr) && !*output) return E_UNEXPECTED;
        if (FAILED(hr)) *output = nullptr;
        return hr;
    }
};

// Source-compatible explicit-IID/LIBID ATL dual-dispatch declaration.
// The optional sixth parameter is an injectable type-info holder (the
// default is the real registered TypeLib loader). A custom holder needs
// GetTI(iid, libid, major, minor, LCID, ITypeInfo**) -> HRESULT.
// This does not implement ATL's image-typelib 0xffff/0xffff mode yet.
// The module-scoped LIBID is populated by DECLARE_LIBID on CAtlModuleT<T>.
template<class T, const IID* piid = &__uuidof(T),
         const GUID* plibid = &CAtlModule::m_libid,
         WORD wMajor = 1, WORD wMinor = 0,
         class tihclass = CComTypeInfoHolder>
class ATL_NO_VTABLE IDispatchImpl : public T {
    static_assert(std::is_base_of<IDispatch, T>::value,
                  "IDispatchImpl<T> requires a dual/dispatch interface");
public:
    using _tihclass = tihclass;

    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* count) noexcept override {
        if (!count) return E_POINTER;
        // ATL's type-info count means a dual-interface description is
        // expected; actual missing/unregistered libraries still fail on
        // GetTypeInfo/GetIDsOfNames/Invoke rather than fake succeeding.
        *count = 1;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT index, LCID locale,
                                           ITypeInfo** output) noexcept override {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (index != 0) return DISP_E_BADINDEX;
        return _tih.GetTI(piid, plibid, wMajor, wMinor, locale, output);
    }

    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID requested,
                                            LPOLESTR* names, UINT count,
                                            LCID locale, DISPID* results) noexcept override {
        if (!names || !results) return E_POINTER;
        if (count == 0 || count > 1024) return E_INVALIDARG;
        for (UINT i = 0; i < count; ++i) {
            results[i] = DISPID_UNKNOWN;
            if (!names[i]) return E_INVALIDARG;
        }
        if (!IsEqualIID(requested, IID_NULL)) return DISP_E_UNKNOWNINTERFACE;
        CComPtr<ITypeInfo> info;
        const HRESULT hr = GetTypeInfo(0, locale, &info.p);
        if (FAILED(hr)) return hr;
        if (!info.p) return E_UNEXPECTED;
        return info->GetIDsOfNames(names, count, results);
    }

    HRESULT STDMETHODCALLTYPE Invoke(DISPID member, REFIID requested,
                                     LCID locale, WORD flags,
                                     DISPPARAMS* parameters, VARIANT* result,
                                     EXCEPINFO* exception, UINT* argument) noexcept override {
        if (!IsEqualIID(requested, IID_NULL)) return DISP_E_UNKNOWNINTERFACE;
        if (!parameters) return E_POINTER;
        if ((parameters->cArgs && !parameters->rgvarg) ||
            (parameters->cNamedArgs && !parameters->rgdispidNamedArgs))
            return E_POINTER;
        if (parameters->cNamedArgs > parameters->cArgs)
            return DISP_E_BADPARAMCOUNT;
        CComPtr<ITypeInfo> info;
        const HRESULT hr = GetTypeInfo(0, locale, &info.p);
        if (FAILED(hr)) return hr;
        if (!info.p) return E_UNEXPECTED;
        // ITypeInfo::Invoke performs native Automation argument conversion,
        // BYREF/EXCEPINFO ownership and VTBL calling-convention dispatch.
        return info->Invoke(static_cast<T*>(this), member, flags,
                            parameters, result, exception, argument);
    }

protected:
    static inline tihclass _tih{};
};

} // namespace ATL
#endif // OPENATL_ATL_DISPATCH_H
