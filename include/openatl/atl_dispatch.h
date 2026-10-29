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

namespace ATL {

class CComTypeInfoHolder {
public:
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
template<class T, const IID* piid, const GUID* plibid,
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
