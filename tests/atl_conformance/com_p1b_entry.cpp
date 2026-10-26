// Independent translation-unit map registration; no OpenATL/RATL mixing.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <objidl.h>
#include <ocidl.h>
#include <atlbase.h>
#include <atlcom.h>

extern "C" const CLSID P1B_CLASS_B = {
    0x2bc20c74,0x9f54,0x41cd,{0x93,0xb9,0x3b,0x87,0x2f,0x07,0x5a,0x22}};

class AutoClassB : public ATL::CComObjectRootEx<ATL::CComSingleThreadModel>,
                   public IPersist {
public:
    BEGIN_COM_MAP(AutoClassB)
        COM_INTERFACE_ENTRY_IID(IID_IPersist, IPersist)
    END_COM_MAP()
    DECLARE_NOT_AGGREGATABLE(AutoClassB)
    STDMETHOD(GetClassID)(CLSID* id) override {
        if(!id) return E_POINTER;
        *id = P1B_CLASS_B;
        return S_OK;
    }
};

OBJECT_ENTRY_AUTO(P1B_CLASS_B, AutoClassB)
