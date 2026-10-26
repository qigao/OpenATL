/* ATL COM P0: neutral fixture. Build this file TWICE, with isolated include trees.
 * This is a COM-map/identity/client-RAII test, NOT CComObject/registry/ActiveX.
 * Never mix OpenATL and RATL headers, object layout, allocations, or binaries.
 */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <objidl.h>
#include <ocidl.h>  // IObjectWithSite used by OpenATL's atlcom.h
#include <cstdio>
#include <atlbase.h>
#include <atlcom.h>
#include <atlcomcli.h>

#if defined(ATL_CONFORMANCE_RATL)
#ifndef __RATL__
#error "RATL conformance build must explicitly use __RATL__"
#endif
#elif defined(ATL_CONFORMANCE_OPENATL)
#ifdef __RATL__
#error "OpenATL conformance must not include RATL"
#endif
#else
#error "Select exactly one ATL provider"
#endif
#if defined(ATL_CONFORMANCE_RATL) && defined(ATL_CONFORMANCE_OPENATL)
#error "Cannot mix ATL implementations"
#endif

// Interfaces deliberately have different multiple-inheritance offsets.
class ProbeBase : public ATL::CComObjectRootEx<ATL::CComSingleThreadModel>,
                  public IPersist, public IStdMarshalInfo {
public:
    BEGIN_COM_MAP(ProbeBase)
        COM_INTERFACE_ENTRY_IID(IID_IPersist, IPersist)
        COM_INTERFACE_ENTRY_IID(IID_IStdMarshalInfo, IStdMarshalInfo)
    END_COM_MAP()

    STDMETHOD(GetClassID)(CLSID* clsid) override {
        if (!clsid) return E_POINTER;
        *clsid = CLSID_NULL;
        return S_OK;
    }
    STDMETHOD(GetClassForHandler)(DWORD, void*, CLSID* clsid) override {
        if (!clsid) return E_POINTER;
        *clsid = CLSID_NULL;
        return S_OK;
    }
};

// A stack-owned, concrete COM object avoids class-factory/registrar paths,
// which are intentionally deferred to P1. COM QI itself is ATL-owned.
class Probe final : public ProbeBase {
public:
    STDMETHOD(QueryInterface)(REFIID iid, void** out) override {
        return _InternalQueryInterface(iid, out);
    }
    STDMETHOD_(ULONG, AddRef)() override { return InternalAddRef(); }
    STDMETHOD_(ULONG, Release)() override { return InternalRelease(); }
};

static int bad(int code, const char* contract) {
    std::fprintf(stderr, "ATL_COM_P0_FAIL code=%d contract=%s\n", code, contract);
    return code;
}

int main() {
    Probe probe;
    if (probe.m_dwRef != 0) return bad(10, "zero_initial_ref");

    IUnknown* canonical = nullptr;
    if (probe.QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&canonical)) != S_OK ||
        canonical == nullptr || probe.m_dwRef != 1)
        return bad(11, "canonical_iunknown");

    IPersist* persist = nullptr;
    IStdMarshalInfo* marshal = nullptr;
    if (probe.QueryInterface(IID_IPersist, reinterpret_cast<void**>(&persist)) != S_OK ||
        persist == nullptr ||
        probe.QueryInterface(IID_IStdMarshalInfo, reinterpret_cast<void**>(&marshal)) != S_OK ||
        marshal == nullptr || probe.m_dwRef != 3)
        return bad(12, "two_mapped_interfaces");

    // A single COM identity despite distinct interface offsets.
    IUnknown *pIdentity = nullptr, *mIdentity = nullptr;
    if (persist->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&pIdentity)) != S_OK ||
        marshal->QueryInterface(IID_IUnknown, reinterpret_cast<void**>(&mIdentity)) != S_OK ||
        pIdentity != canonical || mIdentity != canonical || probe.m_dwRef != 5)
        return bad(13, "identity_across_interfaces");
    pIdentity->Release();
    mIdentity->Release();

    void* unsupported = reinterpret_cast<void*>(static_cast<uintptr_t>(1));
    if (probe.QueryInterface(IID_IClassFactory, &unsupported) != E_NOINTERFACE ||
        unsupported != nullptr || probe.m_dwRef != 3)
        return bad(14, "unsupported_iid_no_ref_leak");

    if (probe.QueryInterface(IID_IPersist, nullptr) != E_POINTER ||
        probe.m_dwRef != 3)
        return bad(15, "null_output_e_pointer");

    CLSID clsid = {};
    if (persist->GetClassID(&clsid) != S_OK ||
        !IsEqualGUID(clsid, CLSID_NULL) ||
        marshal->GetClassForHandler(0, nullptr, &clsid) != S_OK ||
        !IsEqualGUID(clsid, CLSID_NULL))
        return bad(16, "interface_dispatch");

    // Attach adopts one reference; copy increments; Detach does not release.
    ATL::CComPtr<IUnknown> owner;
    owner.Attach(canonical);
    canonical = nullptr;
    {
        ATL::CComPtr<IUnknown> shared(owner);
        if (probe.m_dwRef != 4 || !shared.p || shared.p != owner.p)
            return bad(17, "comptr_copy_addref");
    }
    if (probe.m_dwRef != 3) return bad(18, "comptr_copy_release");

    IUnknown* adopted = owner.Detach();
    if (!adopted || owner.p != nullptr || probe.m_dwRef != 3)
        return bad(19, "comptr_detach");
    if (adopted->Release() != 2 || persist->Release() != 1 ||
        marshal->Release() != 0 || probe.m_dwRef != 0)
        return bad(20, "reference_lifecycle");

    ATL::CComBSTR label(L"ATL-P0");
    if (label.Length() != 6 || label.m_str == nullptr)
        return bad(21, "bstr_length");
    ATL::CComVariant number(42);
    if (number.vt != VT_I4 || number.lVal != 42)
        return bad(22, "variant_integer");

    std::puts("ATL_COM_P0_OK identity=1 map=1 qi_errors=1 refs=0 comptr=1 bstr=1 variant=1");
    return 0;
}
