/* OpenATL COM CLIENT acceptance. No COM class object, activation factory
 * implementation, registry writes, RGS, COM server or RATL dependency.
 * A Windows system GIT component is activated as a client.
 */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <objidl.h>
#include <ocidl.h>
#include <cstdio>
#include <atlbase.h>
#include <atlcomcli.h>

#if defined(__RATL__)
#error "COM client must use the pinned clean-room OpenATL include root"
#endif
static_assert(sizeof(void*) == 8, "Windows x64/ARM64 COM ABI required");

static int failure(int code, const char* step, HRESULT hr) {
    std::fprintf(stderr, "ATL_COM_CLIENT_FAIL code=%d step=%s hr=0x%08lx\n",
                 code, step, static_cast<unsigned long>(hr));
    return code;
}
int main() {
    // Unlike Win32/WTL GUI, a COM client must initialize an apartment.
    HRESULT hr=::CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(hr))return failure(10,"CoInitializeEx",hr);
    int result=0;
    {
        // StdGlobalInterfaceTable is a Windows-provided COM class. We create
        // no application COM server, class factory or persistent registry key.
        IGlobalInterfaceTable* raw=nullptr;
        hr=::CoCreateInstance(CLSID_StdGlobalInterfaceTable,nullptr,
            CLSCTX_INPROC_SERVER,IID_IGlobalInterfaceTable,
            reinterpret_cast<void**>(&raw));
        if(FAILED(hr) || raw==nullptr) {
            result=failure(11,"system_COM_client_activation",hr);
        } else {
            ATL::CComPtr<IGlobalInterfaceTable> git;
            git.Attach(raw);
            raw=nullptr;
            ATL::CComPtr<IGlobalInterfaceTable> shared(git);
            if(!shared.p || shared.p!=git.p)
                result=failure(12,"CComPtr_copy",E_FAIL);
            if(!result) {
                IUnknown* identity=nullptr;
                hr=git->QueryInterface(IID_IUnknown,
                    reinterpret_cast<void**>(&identity));
                if(FAILED(hr) || identity==nullptr)
                    result=failure(13,"QueryInterface_IUnknown",hr);
                else {
                    ATL::CComPtr<IUnknown> owner;
                    owner.Attach(identity);
                    if(!owner.p)result=failure(14,"CComPtr_Attach",E_FAIL);
                    IUnknown* transferred=owner.Detach();
                    if(owner.p!=nullptr || transferred!=identity)
                        result=failure(15,"CComPtr_Detach",E_FAIL);
                    if(transferred)transferred->Release();
                }
            }
            if(!result) {
                ATL::CComBSTR label(L"Client");
                ATL::CComVariant number(42);
                if(label.Length()!=6 || label.m_str==nullptr ||
                   number.vt!=VT_I4 || number.lVal!=42)
                    result=failure(16,"BSTR_Variant",E_FAIL);
            }
        }
    } // All COM smart pointers release before CoUninitialize.
    ::CoUninitialize();
    if(result)return result;
    std::puts("ATL_COM_CLIENT_OK activate=1 qi=1 comptr=1 bstr=1 variant=1 unregister=0");
    return 0;
}
