// OpenATL - clean-room nonaggregated ATL object and COM-map compatibility.
#ifndef __ATLCOM_H__
#define __ATLCOM_H__
#pragma once
#include "atlbase.h"
#include "atlcomcli.h"
#include "openatl/com_server.h"
#include "openatl/rgs_resource.h"
#include "openatl/atl_dispatch.h"
#include <optional>

namespace ATL {

class CComObjectRootBase {
public:
    HRESULT FinalConstruct() noexcept { return S_OK; }
    void FinalRelease() noexcept {}
protected:
    CComObjectRootBase() noexcept = default;
    ~CComObjectRootBase() = default;
};

template<class ThreadModel>
class CComObjectRootEx : public CComObjectRootBase {
public:
    using _ThreadModel = ThreadModel;
    ULONG InternalAddRef() noexcept {
        ULONG count = ThreadModel::Increment(&m_dwRef);
        if (count == 0) std::terminate();
        return count;
    }
    ULONG InternalRelease() noexcept {
        ULONG count = ThreadModel::Decrement(&m_dwRef);
        if (count == (std::numeric_limits<ULONG>::max)()) std::terminate();
        return count;
    }
    void InternalFinalConstructAddRef() noexcept { InternalAddRef(); }
    void InternalFinalConstructRelease() noexcept { InternalRelease(); }
    void Lock() noexcept { m_criticalSection.Lock(); }
    void Unlock() noexcept { m_criticalSection.Unlock(); }
    CComObjectRootEx(const CComObjectRootEx&) = delete;
    CComObjectRootEx& operator=(const CComObjectRootEx&) = delete;
protected:
    CComObjectRootEx() noexcept = default;
    ~CComObjectRootEx() = default;
    LONG m_dwRef = 0;
private:
    typename ThreadModel::AutoCriticalSection m_criticalSection;
};
using CComObjectRoot = CComObjectRootEx<CComObjectThreadModel>;

namespace detail {
// The first base is destroyed last, after the user's Base destructor.
class AtlObjectModulePin {
public:
    AtlObjectModulePin() noexcept : application_(_pAtlModule) {
        if (application_) application_->Lock();
    }
    ~AtlObjectModulePin() {
        reference_.reset();
        if (application_) application_->Unlock();
    }
protected:
    HRESULT BindModule(OpenATL::COM::Module& module) noexcept {
        reference_.emplace(module);
        return reference_->Status();
    }
private:
    CAtlModule* application_;
    std::optional<OpenATL::COM::ModuleReference> reference_;
};
inline HRESULT AtlObjectException() noexcept {
    try { throw; }
    catch (const CAtlException& e) { return FAILED(e.m_hr) ? e.m_hr : E_UNEXPECTED; }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return E_UNEXPECTED; }
}
} // namespace detail

template<class Base>
class CComObject : private detail::AtlObjectModulePin, public Base {
public:
    explicit CComObject(void* = nullptr) {}
    ~CComObject() { this->FinalRelease(); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) noexcept override {
        return this->_InternalQueryInterface(iid, output);
    }
    template<class Q>
    HRESULT QueryInterface(Q** output) noexcept {
        return QueryInterface(__uuidof(Q), reinterpret_cast<void**>(output));
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override { return this->InternalAddRef(); }
    ULONG STDMETHODCALLTYPE Release() noexcept override {
        const ULONG count = this->InternalRelease();
        if (!count) delete this;
        return count;
    }
    // ATL's local creator returns a raw object with reference count ZERO.
    static HRESULT WINAPI CreateInstance(CComObject** output) noexcept {
        return CreateObject(nullptr, output);
    }
    // Explicit DLL creator: the object pins this module through destruction.
    static HRESULT CreateInstance(OpenATL::COM::Module& module, REFIID iid,
                                  void** output) noexcept {
        if (!output) return E_POINTER;
        *output = nullptr;
        CComObject* object = nullptr;
        HRESULT hr = CreateObject(&module, &object);
        if (FAILED(hr)) return hr;
        object->AddRef();
        hr = object->QueryInterface(iid, output);
        object->Release();
        return hr;
    }
private:
    static HRESULT CreateObject(OpenATL::COM::Module* module, CComObject** output) noexcept {
        if (!output) return E_POINTER;
        *output = nullptr;
        CComObject* object = nullptr;
        try { object = new CComObject; }
        catch (...) { return detail::AtlObjectException(); }
        HRESULT hr = module ? object->BindModule(*module) : S_OK;
        if (SUCCEEDED(hr)) {
            // A temporary reference protects QueryInterface/Release inside
            // FinalConstruct; it is removed without deleting the zero-ref object.
            object->InternalFinalConstructAddRef();
            try { hr = object->FinalConstruct(); }
            catch (...) { hr = detail::AtlObjectException(); }
            object->InternalFinalConstructRelease();
        }
        if (FAILED(hr)) { delete object; return hr; }
        *output = object;
        return S_OK;
    }
};

template<class ObjectType>
class CComCreator {
public:
    static HRESULT WINAPI CreateInstance(void* outer, REFIID iid, void** output) noexcept {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        ObjectType* object = nullptr;
        HRESULT hr = ObjectType::CreateInstance(&object);
        if (FAILED(hr)) return hr;
        object->AddRef();
        hr = object->QueryInterface(iid, output);
        object->Release();
        return hr;
    }
};

template<class T, const CLSID* ClassId = &CLSID_NULL>
class CComCoClass {
public:
    static const CLSID& WINAPI GetObjectCLSID() noexcept { return *ClassId; }
    static HRESULT WINAPI CreateInstance(IUnknown* outer, REFIID iid, void** output) noexcept {
        return CComCreator<CComObject<T>>::CreateInstance(outer, iid, output);
    }
    template<class Q>
    static HRESULT WINAPI CreateInstance(Q** output) noexcept {
        return CreateInstance(nullptr, __uuidof(Q), reinterpret_cast<void**>(output));
    }
};
} // namespace ATL

// Maps use real typed casts, including pointer adjustment for multiple
// inheritance. The first simple entry defines the canonical IUnknown.
#define BEGIN_COM_MAP(x) \
public: \
    using _ComMapClass = x; \
    HRESULT _InternalQueryInterface(REFIID iid, void** output) noexcept { \
        HRESULT hr = _OpenATLFindInterface(iid, output); \
        if (SUCCEEDED(hr)) this->InternalAddRef(); \
        return hr; \
    } \
    IUnknown* GetUnknown() noexcept { \
        void* output = nullptr; \
        _OpenATLFindInterface(IID_IUnknown, &output); \
        return static_cast<IUnknown*>(output); \
    } \
    HRESULT _OpenATLFindInterface(REFIID iid, void** output) noexcept { \
        if (!output) return E_POINTER; \
        *output = nullptr; \
        bool _atlFirst = true;

#define COM_INTERFACE_ENTRY_IID(id, x) \
    { x* _atlInterface = static_cast<x*>(this); \
      if ((_atlFirst && IsEqualIID(iid, IID_IUnknown)) || IsEqualIID(iid, id)) { \
          *output = IsEqualIID(iid, IID_IUnknown) ? \
              static_cast<void*>(static_cast<IUnknown*>(_atlInterface)) : static_cast<void*>(_atlInterface); \
          return S_OK; \
      } _atlFirst = false; }

#define COM_INTERFACE_ENTRY(x) COM_INTERFACE_ENTRY_IID(__uuidof(x), x)
#define COM_INTERFACE_ENTRY2_IID(id, x, via) \
    { x* _atlInterface = static_cast<x*>(static_cast<via*>(this)); \
      if ((_atlFirst && IsEqualIID(iid, IID_IUnknown)) || IsEqualIID(iid, id)) { \
          *output = IsEqualIID(iid, IID_IUnknown) ? \
              static_cast<void*>(static_cast<IUnknown*>(_atlInterface)) : static_cast<void*>(_atlInterface); \
          return S_OK; \
      } _atlFirst = false; }
#define COM_INTERFACE_ENTRY2(x, via) COM_INTERFACE_ENTRY2_IID(__uuidof(x), x, via)
// Chain to an existing ATL base COM map without invoking QueryInterface:
// the outermost CComObject::QueryInterface adds the one owning reference.
// A chained map only supplies a borrowed, correctly adjusted interface ptr.
// In particular, the outer map's first simple entry still defines the
// canonical IUnknown; a chain-only outer map inherits its base identity.
#define COM_INTERFACE_ENTRY_CHAIN(base) \
    { \
        void* _atlChained = nullptr; \
        const HRESULT _atlChainHr = \
            static_cast<base*>(this)->_OpenATLFindInterface(iid, &_atlChained); \
        if (_atlChainHr == S_OK && _atlChained) { \
            *output = _atlChained; \
            return S_OK; \
        } \
        if (SUCCEEDED(_atlChainHr)) return E_UNEXPECTED; \
        if (_atlChainHr != E_NOINTERFACE) return _atlChainHr; \
    }
#define END_COM_MAP() \
        (void)_atlFirst; \
        return E_NOINTERFACE; \
    }
// Standard ATL-style automatic COM object-map declaration. The referenced
// CComObject<T> creator binds the existing single OpenATL::COM::Module,
// preserving object/factory/server-lock ownership. The registration record
// is static binary-owned metadata; no COM call or heap allocation occurs
// in its constructor under the Windows loader lock.
#define _OPENATL_AUTO_CAT_IMPL(a, b) a##b
#define _OPENATL_AUTO_CAT(a, b) _OPENATL_AUTO_CAT_IMPL(a, b)
#define OBJECT_ENTRY_AUTO(clsid, type) \
    namespace { \
        ATL::detail::AutoObjectEntry _OPENATL_AUTO_CAT(_openatlAutoEntry_, __COUNTER__)( \
            (clsid), static_cast<OpenATL::COM::Creator>( \
                &ATL::CComObject<type>::CreateInstance), \
            ATL::detail::AutoRegistryMethod<type>::callback); \
    }

// Class-directed original ATL REGISTRY resource-ID declaration. The owning
// DLL export must first supply a ScopedRgsRegistrationContext with an
// explicit opened Classes HKEY, image and owner identity. No HKCR default.
#define DECLARE_REGISTRY_RESOURCEID(id) \
public: \
    static HRESULT WINAPI UpdateRegistry(BOOL install) noexcept { \
        return ::OpenATL::COM::UpdateRegistryResource( \
            static_cast<unsigned>(id), install); \
    }

// ATL's deliberate declaration that this class performs no persistent
// registration. This is opt-in, unlike a missing/unsupported registrar.
#define DECLARE_NO_REGISTRY() \
public: \
    static HRESULT WINAPI UpdateRegistry(BOOL) noexcept { return S_OK; }

#define DECLARE_NOT_AGGREGATABLE(x) \
    using _CreatorClass = ATL::CComCreator<ATL::CComObject<x>>;
#define DECLARE_PROTECT_FINAL_CONSTRUCT() \
    void InternalFinalConstructAddRef() noexcept { this->InternalAddRef(); } \
    void InternalFinalConstructRelease() noexcept { this->InternalRelease(); }
#include "openatl/atl_events.h"
#endif
