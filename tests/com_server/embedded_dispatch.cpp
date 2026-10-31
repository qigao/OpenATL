// Actual DLL-image Automation over unchanged Microsoft COMMap MIDL output.
// These are test-fixture exports, not new OpenWTL public APIs.
#include <atlcom.h>
#if defined(OPENATL_ORIGINAL_COMMAP_MIDL)
#include "COMMap.h"

namespace {
template<WORD Major, WORD Minor>
class EmbeddedChain : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>,
                      public ATL::IDispatchImpl<IChain, &IID_IChain,
                          &LIBID_COMMAPLib, Major, Minor> {
public:
    BEGIN_COM_MAP(EmbeddedChain)
        COM_INTERFACE_ENTRY_IID(IID_IChain, IChain)
        COM_INTERFACE_ENTRY(IDispatch)
    END_COM_MAP()
    HRESULT STDMETHODCALLTYPE get_Name2(BSTR* output) noexcept override {
        if (!output) return E_POINTER;
        *output = ::SysAllocString(L"OpenWTL embedded chain");
        return *output ? S_OK : E_OUTOFMEMORY;
    }
};
}
#endif

template<WORD Major, WORD Minor>
HRESULT CreateDispatch(IDispatch** output) {
    if (!output) return E_POINTER;
    *output = nullptr;
#if defined(OPENATL_ORIGINAL_COMMAP_MIDL)
    using Object = ATL::CComObject<EmbeddedChain<Major, Minor>>;
    Object* raw = nullptr;
    HRESULT hr = Object::CreateInstance(&raw);
    if (FAILED(hr)) return hr;
    raw->AddRef();
    hr = raw->QueryInterface(IID_IDispatch, reinterpret_cast<void**>(output));
    raw->Release();
    return hr;
#else
    return E_NOTIMPL;
#endif
}

extern "C" HRESULT WINAPI OpenATLCreateEmbeddedDispatch(IDispatch** output) {
    return CreateDispatch<0xffff, 0xffff>(output);
}
extern "C" HRESULT WINAPI OpenATLCreateRegisteredDispatch(IDispatch** output) {
    return CreateDispatch<1, 0>(output);
}

extern "C" HRESULT WINAPI OpenATLGetEmbeddedTypeInfo(
    REFIID iid, REFGUID libid, ITypeInfo** output) {
    ATL::CComTypeInfoHolder holder;
    return holder.GetTI(&iid, &libid, 0xffff, 0xffff,
                        LOCALE_SYSTEM_DEFAULT, output);
}
