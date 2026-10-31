// OpenATL public containment and registrar interfaces. COM vtable order
// follows the published ATL IDL; no external ATL implementation is used.
#ifndef OPENATL_ATLIFACE_H
#define OPENATL_ATLIFACE_H
#pragma once
#include <olectl.h>
#ifndef __IRegistrarBase_INTERFACE_DEFINED__
#define __IRegistrarBase_INTERFACE_DEFINED__
inline constexpr IID IID_IRegistrarBase = {0xe21f8a85,0xb05d,0x4243,{0x81,0x83,0xc7,0xcb,0x40,0x55,0x88,0xf7}};
MIDL_INTERFACE("E21F8A85-B05D-4243-8183-C7CB405588F7") IRegistrarBase : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE AddReplacement(LPCOLESTR, LPCOLESTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE ClearReplacements() = 0;
};
#ifdef __CRT_UUID_DECL
__CRT_UUID_DECL(IRegistrarBase,0xe21f8a85,0xb05d,0x4243,0x81,0x83,0xc7,0xcb,0x40,0x55,0x88,0xf7)
#endif
#endif
struct IDocHostUIHandlerDispatch;
inline constexpr IID IID_IAxWinHostWindow = {0xb6ea2050,0x048a,0x11d1,{0x82,0xb9,0x00,0xc0,0x4f,0xb9,0x94,0x2e}};
inline constexpr IID IID_IAxWinAmbientDispatch = {0xb6ea2051,0x048a,0x11d1,{0x82,0xb9,0x00,0xc0,0x4f,0xb9,0x94,0x2e}};
MIDL_INTERFACE("B6EA2050-048A-11D1-82B9-00C04FB9942E") IAxWinHostWindow : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE CreateControl(LPCOLESTR, HWND, IStream*) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateControlEx(LPCOLESTR, HWND, IStream*, IUnknown**, REFIID, IUnknown*) = 0;
    virtual HRESULT STDMETHODCALLTYPE AttachControl(IUnknown*, HWND) = 0;
    virtual HRESULT STDMETHODCALLTYPE QueryControl(REFIID, void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetExternalDispatch(IDispatch*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetExternalUIHandler(IDocHostUIHandlerDispatch*) = 0;
};
MIDL_INTERFACE("B6EA2051-048A-11D1-82B9-00C04FB9942E") IAxWinAmbientDispatch : IDispatch {
    virtual HRESULT STDMETHODCALLTYPE put_AllowWindowlessActivation(VARIANT_BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AllowWindowlessActivation(VARIANT_BOOL*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_BackColor(OLE_COLOR) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_BackColor(OLE_COLOR*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_ForeColor(OLE_COLOR) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ForeColor(OLE_COLOR*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_LocaleID(LCID) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_LocaleID(LCID*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_UserMode(VARIANT_BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_UserMode(VARIANT_BOOL*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_DisplayAsDefault(VARIANT_BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_DisplayAsDefault(VARIANT_BOOL*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Font(IFontDisp*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Font(IFontDisp**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_MessageReflect(VARIANT_BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_MessageReflect(VARIANT_BOOL*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ShowGrabHandles(VARIANT_BOOL*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ShowHatching(VARIANT_BOOL*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_DocHostFlags(DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_DocHostFlags(DWORD*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_DocHostDoubleClickFlags(DWORD) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_DocHostDoubleClickFlags(DWORD*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AllowContextMenu(VARIANT_BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AllowContextMenu(VARIANT_BOOL*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_AllowShowUI(VARIANT_BOOL) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_AllowShowUI(VARIANT_BOOL*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_OptionKeyPath(BSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_OptionKeyPath(BSTR*) = 0;
};
#ifdef __CRT_UUID_DECL
__CRT_UUID_DECL(IAxWinHostWindow,0xb6ea2050,0x048a,0x11d1,0x82,0xb9,0x00,0xc0,0x4f,0xb9,0x94,0x2e)
__CRT_UUID_DECL(IAxWinAmbientDispatch,0xb6ea2051,0x048a,0x11d1,0x82,0xb9,0x00,0xc0,0x4f,0xb9,0x94,0x2e)
#endif
#endif
