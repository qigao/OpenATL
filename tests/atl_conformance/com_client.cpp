/* OpenATL optional COM CLIENT: apartment ownership and actual Windows
 * system COM activation. No app-owned COM class, server, registrar or RGS.
 *
 * Each invocation is a fresh process/thread, making apartment checks
 * independent and permitting exact repetition on Windows x64 and ARM64.
 */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <objidl.h>
#include <ocidl.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <atlbase.h>
#include <atlcomcli.h>

#if defined(__RATL__)
#error "COM client must use pinned clean-room OpenATL headers"
#endif
static_assert(sizeof(void*) == 8, "Windows x64/ARM64 COM ABI required");

static int failure(int code, const char* step, HRESULT hr) {
    std::fprintf(stderr, "ATL_COM_CLIENT_FAIL code=%d step=%s hr=0x%08lx\n",
                 code, step, static_cast<unsigned long>(hr));
    return code;
}
static bool no_apartment() {
    ULONG_PTR context=0;
    return ::CoGetContextToken(&context)==CO_E_NOTINITIALIZED;
}

// IGlobalInterfaceTable is an OS-provided COM *client* target, not our own
// factory, service or registration. No COM object survives apartment exit.
static int system_component_client() {
    IGlobalInterfaceTable* raw=nullptr;
    HRESULT hr=::CoCreateInstance(CLSID_StdGlobalInterfaceTable,nullptr,
        CLSCTX_INPROC_SERVER,IID_IGlobalInterfaceTable,
        reinterpret_cast<void**>(&raw));
    if(FAILED(hr)||raw==nullptr)return failure(11,"CoCreateInstance_GIT",hr);
    {
        ATL::CComPtr<IGlobalInterfaceTable> git;
        git.Attach(raw);
        raw=nullptr;
        ATL::CComPtr<IGlobalInterfaceTable> shared(git);
        if(!shared.p || shared.p!=git.p)
            return failure(12,"CComPtr_copy",E_FAIL);

        IUnknown* identity=nullptr;
        hr=git->QueryInterface(IID_IUnknown,
               reinterpret_cast<void**>(&identity));
        if(FAILED(hr)||identity==nullptr)
            return failure(13,"QueryInterface_IUnknown",hr);
        ATL::CComPtr<IUnknown> owner;
        owner.Attach(identity);
        if(owner.p!=identity)return failure(14,"CComPtr_Attach",E_FAIL);
        IUnknown* detached=owner.Detach();
        if(owner.p!=nullptr||detached!=identity)
            return failure(15,"CComPtr_Detach",E_FAIL);
        detached->Release();

        ATL::CComBSTR name(L"Client");
        ATL::CComVariant number(42);
        if(name.Length()!=6 || name.m_str==nullptr ||
           number.vt!=VT_I4 || number.lVal!=42)
            return failure(16,"BSTR_and_VARIANT",E_FAIL);
    }
    return 0;
}

static int run_uninitialized() {
    if(!no_apartment())return failure(20,"unexpected_initial_apartment",E_FAIL);
    IGlobalInterfaceTable* object=nullptr;
    const HRESULT hr=::CoCreateInstance(CLSID_StdGlobalInterfaceTable,nullptr,
        CLSCTX_INPROC_SERVER,IID_IGlobalInterfaceTable,
        reinterpret_cast<void**>(&object));
    if(hr!=CO_E_NOTINITIALIZED || object!=nullptr) {
        if(object)object->Release();
        return failure(21,"activation_requires_apartment",hr);
    }
    if(!no_apartment())return failure(22,"negative_test_implicitly_initialized",E_FAIL);
    std::puts("ATL_COM_CLIENT_MODE_OK mode=uninitialized apartment=0 activation_denied=1");
    return 0;
}

static int run_apartment(const char* mode,bool mta,bool changed) {
    if(!no_apartment())return failure(30,"fresh_thread_uninitialized",E_FAIL);
    const DWORD flags=mta?COINIT_MULTITHREADED:COINIT_APARTMENTTHREADED;
    const DWORD opposite=mta?COINIT_APARTMENTTHREADED:COINIT_MULTITHREADED;
    HRESULT hr=::CoInitializeEx(nullptr,flags);
    if(hr!=S_OK)return failure(31,"first_CoInitializeEx",hr);

    int result=0;
    ULONG_PTR context=0;
    hr=::CoGetContextToken(&context);
    if(hr!=S_OK)
        result=failure(32,"apartment_context_token",hr);

    if(!result&&changed) {
        hr=::CoInitializeEx(nullptr,opposite);
        if(hr!=RPC_E_CHANGED_MODE) {
            // Defensive cleanup if an unexpected successful initialization
            // was returned. A failed changed-mode call needs no Uninit.
            if(SUCCEEDED(hr))::CoUninitialize();
            result=failure(33,"changed_apartment_model_must_fail",hr);
        }
        if(!result && ::CoGetContextToken(&context)!=S_OK)
            result=failure(34,"changed_mode_damaged_apartment",E_FAIL);
    }

    if(!result) {
        // Same model returns S_FALSE and still needs its own CoUninitialize.
        hr=::CoInitializeEx(nullptr,flags);
        if(hr!=S_FALSE) {
            if(SUCCEEDED(hr))::CoUninitialize();
            result=failure(35,"same_model_nested_initialize",hr);
        } else {
            ::CoUninitialize();
            if(::CoGetContextToken(&context)!=S_OK)
                result=failure(36,"nested_CoUninitialize_unbalanced",E_FAIL);
        }
    }
    if(!result)result=system_component_client();

    ::CoUninitialize(); // Exactly once for the original S_OK
    if(!result && !no_apartment())
        result=failure(37,"COM_apartment_leaked",E_FAIL);
    if(result)return result;

    // Keep the original default output stable for legacy consumer tests.
    if(std::strcmp(mode,"default")==0)
        std::puts("ATL_COM_CLIENT_OK activate=1 qi=1 comptr=1 bstr=1 variant=1 unregister=0");
    else
        std::printf("ATL_COM_CLIENT_MODE_OK mode=%s apartment=1 activation=1 balance=1 changed=%d\n",
                    mode,changed?1:0);
    return 0;
}

int main(int argc,char** argv) {
    if(argc==1)return run_apartment("default",false,false);
    if(argc!=2)return failure(40,"unexpected_arguments",E_INVALIDARG);
    const char* mode=argv[1];
    if(std::strcmp(mode,"--sta")==0)
        return run_apartment("sta",false,false);
    if(std::strcmp(mode,"--mta")==0)
        return run_apartment("mta",true,false);
    if(std::strcmp(mode,"--changed-sta")==0)
        return run_apartment("changed-sta",false,true);
    if(std::strcmp(mode,"--changed-mta")==0)
        return run_apartment("changed-mta",true,true);
    if(std::strcmp(mode,"--uninitialized")==0)
        return run_uninitialized();
    return failure(41,"unsupported_mode",E_INVALIDARG);
}
