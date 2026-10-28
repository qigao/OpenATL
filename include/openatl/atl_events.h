// OpenATL: typed Automation event sinks, independent of member-pointer ABI.
#ifndef OPENATL_ATL_EVENTS_H
#define OPENATL_ATL_EVENTS_H
#pragma once
#include <ocidl.h>
#include <olectl.h>
#include <array>
#include <functional>
#include <type_traits>
#include <utility>

namespace ATL {
struct _ATL_FUNC_INFO {
    CALLCONV cc;
    VARTYPE vtReturn;
    SHORT nParams;
    VARTYPE pVarTypes[8];
};

inline HRESULT AtlAdvise(IUnknown* source, IUnknown* sink, const IID& iid, DWORD* cookie) noexcept {
    if (!cookie) return E_POINTER;
    *cookie = 0;
    if (!source || !sink) return E_POINTER;
    CComPtr<IConnectionPointContainer> container;
    HRESULT hr = source->QueryInterface(IID_IConnectionPointContainer, reinterpret_cast<void**>(&container));
    if (FAILED(hr)) return hr;
    CComPtr<IConnectionPoint> point;
    hr = container->FindConnectionPoint(iid, &point);
    return FAILED(hr) ? hr : point->Advise(sink, cookie);
}
inline HRESULT AtlUnadvise(IUnknown* source, const IID& iid, DWORD cookie) noexcept {
    if (!source) return E_POINTER;
    CComPtr<IConnectionPointContainer> container;
    HRESULT hr = source->QueryInterface(IID_IConnectionPointContainer, reinterpret_cast<void**>(&container));
    if (FAILED(hr)) return hr;
    CComPtr<IConnectionPoint> point;
    hr = container->FindConnectionPoint(iid, &point);
    return FAILED(hr) ? hr : point->Unadvise(cookie);
}

namespace detail {
struct EventEntry {
    UINT control;
    const IID* iid;
    DISPID member;
    const _ATL_FUNC_INFO* info;
    HRESULT (*call)(void*, DISPPARAMS*, const _ATL_FUNC_INFO*, UINT*);
};
template<class A> bool EventAccepts(VARTYPE type) {
    if constexpr (std::is_same_v<A, BSTR>) return type == VT_BSTR;
    else if constexpr (std::is_same_v<A, IDispatch*>) return type == VT_DISPATCH;
    else if constexpr (std::is_same_v<A, IUnknown*>) return type == VT_UNKNOWN;
    else if constexpr (std::is_same_v<A, VARIANT*>) return type == (VT_BYREF | VT_VARIANT);
    else if constexpr (std::is_same_v<A, VARIANT>) return type == VT_VARIANT;
    else if constexpr (std::is_pointer_v<A>) return (type & VT_BYREF) && EventAccepts<std::remove_pointer_t<A>>(type & ~VT_BYREF);
    else if constexpr (std::is_integral_v<A>) {
        if constexpr (sizeof(A) == 1) return type == VT_I1 || type == VT_UI1;
        if constexpr (sizeof(A) == 2) return type == VT_I2 || type == VT_UI2 || type == VT_BOOL;
        if constexpr (sizeof(A) == 4) return type == VT_I4 || type == VT_UI4 || type == VT_INT || type == VT_UINT;
        if constexpr (sizeof(A) == 8) return type == VT_I8 || type == VT_UI8;
        return false;
    } else if constexpr (std::is_floating_point_v<A>) {
        if constexpr (sizeof(A) == 4) return type == VT_R4;
        if constexpr (sizeof(A) == 8) return type == VT_R8 || type == VT_DATE;
        return false;
    }
    else return false;
}
template<class A> A EventArgument(VARIANTARG& v) {
    if constexpr (std::is_same_v<A, BSTR>) return v.bstrVal;
    else if constexpr (std::is_same_v<A, IDispatch*>) return v.pdispVal;
    else if constexpr (std::is_same_v<A, IUnknown*>) return v.punkVal;
    else if constexpr (std::is_same_v<A, VARIANT*>) return v.pvarVal;
    else if constexpr (std::is_same_v<A, VARIANT>) return v;
    else if constexpr (std::is_pointer_v<A>) return static_cast<A>(v.byref);
    else {
        switch (v.vt) {
        case VT_I1: return static_cast<A>(v.cVal);
        case VT_UI1: return static_cast<A>(v.bVal);
        case VT_I2: return static_cast<A>(v.iVal);
        case VT_UI2: return static_cast<A>(v.uiVal);
        case VT_BOOL: return static_cast<A>(v.boolVal);
        case VT_I4: case VT_INT: return static_cast<A>(v.lVal);
        case VT_UI4: case VT_UINT: return static_cast<A>(v.ulVal);
        case VT_I8: return static_cast<A>(v.llVal);
        case VT_UI8: return static_cast<A>(v.ullVal);
        case VT_R4: return static_cast<A>(v.fltVal);
        case VT_R8: case VT_DATE: return static_cast<A>(v.dblVal);
        default: return A{};
        }
    }
}
template<class R, class C, class... A> struct EventSignature {
    template<auto Method, size_t... I>
    static HRESULT Call(void* object, DISPPARAMS* params, const _ATL_FUNC_INFO* info,
                        UINT* argument, std::index_sequence<I...>) {
        if (!info || info->nParams != sizeof...(A) || info->nParams > 8 ||
            (info->cc != CC_STDCALL && info->cc != CC_CDECL)) return E_INVALIDARG;
        if (!(EventAccepts<A>(info->pVarTypes[I]) && ...)) return E_INVALIDARG;
        if (!params || params->cArgs != sizeof...(A)) return DISP_E_BADPARAMCOUNT;
        if (params->cNamedArgs) return DISP_E_NONAMEDARGS;
        if (params->cArgs && !params->rgvarg) return E_POINTER;
        struct Arguments {
            std::array<VARIANTARG, sizeof...(A)> values{};
            ~Arguments() { for (auto& value : values) ::VariantClear(&value); }
        } converted;
        for (UINT i = 0; i < sizeof...(A); ++i) {
            const UINT reverse = params->cArgs - 1 - i;
            VARIANTARG& original = params->rgvarg[reverse];
            const VARTYPE type = info->pVarTypes[i];
            HRESULT hr;
            if (type & VT_BYREF) {
                if (original.vt != type || !original.byref) hr = DISP_E_TYPEMISMATCH;
                else { converted.values[i] = original; hr = S_OK; }
            } else if (type == VT_VARIANT) {
                hr = ::VariantCopyInd(&converted.values[i], &original);
            } else {
                hr = ::VariantChangeType(&converted.values[i], &original, 0, type);
            }
            if (FAILED(hr)) { if (argument) *argument = reverse; return hr; }
        }
        if constexpr (std::is_void_v<R>) {
            std::invoke(Method, *static_cast<C*>(object), EventArgument<A>(converted.values[I])...);
            return S_OK;
        } else {
            static_assert(std::is_same_v<R, HRESULT>, "Event handlers return void or HRESULT");
            return std::invoke(Method, *static_cast<C*>(object), EventArgument<A>(converted.values[I])...);
        }
    }
};
template<class M> struct EventTraits;
template<class R, class C, class... A>
struct EventTraits<R (C::*)(A...)> : EventSignature<R, C, A...> {
    using Sequence = std::index_sequence_for<A...>;
};
#if defined(_M_IX86) || defined(__i386__)
template<class R, class C, class... A>
struct EventTraits<R (__stdcall C::*)(A...)> : EventSignature<R, C, A...> {
    using Sequence = std::index_sequence_for<A...>;
};
#endif
template<auto Method> HRESULT InvokeEvent(void* object, DISPPARAMS* params,
                                          const _ATL_FUNC_INFO* info, UINT* argument) {
    using Traits = EventTraits<decltype(Method)>;
    try { return Traits::template Call<Method>(object, params, info, argument, typename Traits::Sequence{}); }
    catch (const CAtlException& e) { return e.m_hr; }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return DISP_E_EXCEPTION; }
}
} // namespace detail

// As in ATL, the containing C++ object owns this sink's lifetime. Unadvise
// before destroying it; AddRef/Release do not delete a window or stack owner.
template<UINT nID, class T, const IID* pdiid>
class IDispEventSimpleImpl : public IDispatch {
public:
    IDispEventSimpleImpl() noexcept : iid_(IID_NULL) {
        if constexpr (pdiid != nullptr) iid_ = *pdiid;
    }
    IDispEventSimpleImpl(const IDispEventSimpleImpl&) = delete;
    IDispEventSimpleImpl& operator=(const IDispEventSimpleImpl&) = delete;
    ~IDispEventSimpleImpl() { if (point_) point_->Unadvise(m_dwEventCookie); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) override {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (iid != IID_IUnknown && iid != IID_IDispatch && iid != iid_) return E_NOINTERFACE;
        *output = static_cast<IDispatch*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* count) override {
        if (count) *count = 0;
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo** info) override {
        if (info) *info = nullptr;
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID member, REFIID iid, LCID, WORD flags, DISPPARAMS* params,
                                     VARIANT* result, EXCEPINFO*, UINT* argument) override {
        if (iid != IID_NULL) return DISP_E_UNKNOWNINTERFACE;
        if (!(flags & DISPATCH_METHOD)) return DISP_E_MEMBERNOTFOUND;
        if (result) ::VariantInit(result);
        for (auto entry = T::_GetSinkMap(); entry->iid; ++entry)
            if (entry->control == nID && *entry->iid == iid_ && entry->member == member)
                return entry->call(static_cast<T*>(this), params, entry->info, argument);
        return DISP_E_MEMBERNOTFOUND;
    }
    HRESULT DispEventAdvise(IUnknown* source, const IID* iid = pdiid) noexcept {
        if (!source) return E_POINTER;
        if (changing_) return E_UNEXPECTED;
        if (point_) return CONNECT_E_ADVISELIMIT;
        Change guard(changing_);
        IID selected = iid ? *iid : IID_NULL;
        if (selected == IID_NULL) {
            CComPtr<IProvideClassInfo2> info;
            HRESULT hr = source->QueryInterface(IID_IProvideClassInfo2, reinterpret_cast<void**>(&info));
            if (FAILED(hr)) return hr;
            hr = info->GetGUID(GUIDKIND_DEFAULT_SOURCE_DISP_IID, &selected);
            if (FAILED(hr)) return hr;
        }
        CComPtr<IConnectionPointContainer> container;
        HRESULT hr = source->QueryInterface(IID_IConnectionPointContainer, reinterpret_cast<void**>(&container));
        if (FAILED(hr)) return hr;
        CComPtr<IConnectionPoint> point;
        hr = container->FindConnectionPoint(selected, &point);
        if (FAILED(hr)) return hr;
        iid_ = selected;
        hr = point->Advise(static_cast<IDispatch*>(this), &m_dwEventCookie);
        if (SUCCEEDED(hr)) point_ = point;
        return hr;
    }
    HRESULT DispEventUnadvise(IUnknown* source, const IID* iid = pdiid) noexcept {
        if (!source) return E_POINTER;
        if (changing_) return E_UNEXPECTED;
        if (!point_) return CONNECT_E_NOCONNECTION;
        Change guard(changing_);
        if (iid && *iid != iid_) return CONNECT_E_NOCONNECTION;
        CComPtr<IConnectionPointContainer> owner;
        HRESULT hr = point_->GetConnectionPointContainer(&owner);
        if (FAILED(hr)) return hr;
        CComPtr<IUnknown> actual, requested;
        hr = owner->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&actual));
        if (FAILED(hr)) return hr;
        hr = source->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&requested));
        if (FAILED(hr)) return hr;
        if (actual.p != requested.p) return CONNECT_E_NOCONNECTION;
        hr = point_->Unadvise(m_dwEventCookie);
        if (SUCCEEDED(hr)) { point_.Release(); m_dwEventCookie = 0xFEFEFEFE; }
        return hr;
    }
    HRESULT Advise(IUnknown* source) noexcept { return DispEventAdvise(source); }
    HRESULT Unadvise(IUnknown* source) noexcept { return DispEventUnadvise(source); }
    DWORD m_dwEventCookie = 0xFEFEFEFE;
private:
    struct Change { bool& flag; explicit Change(bool& value) : flag(value) { flag = true; } ~Change() { flag = false; } };
    bool changing_ = false;
    IID iid_;
    CComPtr<IConnectionPoint> point_;
};
} // namespace ATL

#define BEGIN_SINK_MAP(x) \
public: \
    static const ATL::detail::EventEntry* _GetSinkMap() { \
        using _SinkClass = x; \
        static const ATL::detail::EventEntry entries[] = {
#define SINK_ENTRY_INFO(id, iid, member, handler, info) \
    { id, &(iid), member, info, &ATL::detail::InvokeEvent<&_SinkClass::handler> },
#define END_SINK_MAP() \
    { 0, nullptr, 0, nullptr, nullptr } }; return entries; }
#endif
