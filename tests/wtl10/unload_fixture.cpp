// Real source-owned OpenATL thunk in a dynamically loaded Windows DLL.
// STOP fences submissions, drains posted messages, retires HWND, unregisters
// the DLL's window class, and joins the owner thread before FreeLibrary.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <atlwin.h>

#if !defined(OPENATL_REPOSITORY_HEADERS) || OPENATL_REPOSITORY_HEADERS != 0x4F41544CUL
#error "Use source-owned OpenATL headers"
#endif

namespace {
enum : LONG { idle=0, starting=1, live=2, closing=3, quiesced=4, failed=5 };
constexpr UINT work_message=WM_APP+87, stop_message=WM_APP+88;
constexpr wchar_t class_name[]=L"OpenATL_WTL10_UnloadAdmission";

SRWLOCK gate=SRWLOCK_INIT;
HANDLE thread_handle=nullptr, ready_event=nullptr;
HWND live_window=nullptr;
volatile LONG phase=idle, accepted=0, processed=0, callbacks=0;
volatile LONG finalized=0, destroyed=0, released=0, unregistered=0;

LONG read(volatile LONG* value) noexcept {
    return ::InterlockedCompareExchange(value,0,0);
}
struct CallbackLease {
    CallbackLease() noexcept { ::InterlockedIncrement(&callbacks); }
    ~CallbackLease() { ::InterlockedDecrement(&callbacks); }
};
class ProbeWindow final : public ATL::CWindowImpl<ProbeWindow> {
public:
    DECLARE_WND_CLASS(class_name)
    BEGIN_MSG_MAP(ProbeWindow)
        MESSAGE_HANDLER(work_message, OnWork)
        MESSAGE_HANDLER(stop_message, OnStop)
    END_MSG_MAP()

    LRESULT OnWork(UINT,WPARAM,LPARAM,BOOL&) {
        CallbackLease lease;
        ::InterlockedIncrement(&processed);
        return 0;
    }
    LRESULT OnStop(UINT,WPARAM,LPARAM,BOOL&) {
        CallbackLease lease;
        HWND own=m_hWnd;
        const BOOL done=own && ::DestroyWindow(own);
        ::InterlockedExchange(&destroyed,done && !::IsWindow(own)?1:0);
        ::PostQuitMessage(done?0:100);
        return 0;
    }
    void OnFinalMessage(HWND) override {
        ::InterlockedIncrement(&finalized);
    }
};

DWORD WINAPI ui_thread(void*) {
    DWORD result=100;
    void* thunk_address=nullptr;
    {
        ProbeWindow wnd;
        RECT area{0,0,64,64};
        HWND hwnd=wnd.Create(nullptr,area,L"OpenATL unload probe",WS_POPUP,0);
        if(hwnd && ::IsWindow(hwnd)) {
            thunk_address=wnd.m_thunk.GetCodeAddress();
            MEMORY_BASIC_INFORMATION info{};
            bool rx=thunk_address &&
                ::VirtualQuery(thunk_address,&info,sizeof(info))==sizeof(info) &&
                info.State==MEM_COMMIT && (info.Protect&0xffu)==PAGE_EXECUTE_READ;
            if(rx && reinterpret_cast<WNDPROC>(
                   ::GetWindowLongPtrW(hwnd,GWLP_WNDPROC))==wnd.m_thunk.GetWNDPROC()) {
                ::AcquireSRWLockExclusive(&gate);
                live_window=hwnd;
                ::InterlockedExchange(&phase,live);
                ::ReleaseSRWLockExclusive(&gate);
                ::SetEvent(ready_event);
                MSG message{};
                BOOL got=0;
                while((got=::GetMessageW(&message,nullptr,0,0))>0) {
                    ::TranslateMessage(&message);
                    ::DispatchMessageW(&message);
                }
                result=(got==0 && message.wParam==0 &&
                        read(&destroyed)==1 && read(&finalized)==1)?0:101;
            } else {
                (void)::DestroyWindow(hwnd);
                ::SetEvent(ready_event);
            }
        } else {
            ::SetEvent(ready_event);
        }
        // wnd's destructor releases its executable thunk after callback exit.
    }
    if(thunk_address) {
        MEMORY_BASIC_INFORMATION info{};
        if(::VirtualQuery(thunk_address,&info,sizeof(info))==sizeof(info) &&
           info.State==MEM_FREE)
            ::InterlockedExchange(&released,1);
    }
    // Never leave a registered class with a WNDPROC pointing into the DLL.
    if(::UnregisterClassW(class_name,ATL::_AtlBaseModule.GetModuleInstance()))
        ::InterlockedExchange(&unregistered,1);
    else
        result=102;
    ::AcquireSRWLockExclusive(&gate);
    live_window=nullptr;
    ::ReleaseSRWLockExclusive(&gate);
    return result;
}
} // namespace

extern "C" __declspec(dllexport) DWORD WINAPI OpenATLUnloadStart() {
    ::AcquireSRWLockExclusive(&gate);
    if((read(&phase)!=idle && read(&phase)!=quiesced) ||
       thread_handle || ready_event) {
        ::ReleaseSRWLockExclusive(&gate);
        return ERROR_BUSY;
    }
    ::InterlockedExchange(&accepted,0);
    ::InterlockedExchange(&processed,0);
    ::InterlockedExchange(&callbacks,0);
    ::InterlockedExchange(&finalized,0);
    ::InterlockedExchange(&destroyed,0);
    ::InterlockedExchange(&released,0);
    ::InterlockedExchange(&unregistered,0);
    ::InterlockedExchange(&phase,starting);
    ready_event=::CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!ready_event) {
        DWORD error=::GetLastError();
        ::InterlockedExchange(&phase,failed);
        ::ReleaseSRWLockExclusive(&gate);
        return error;
    }
    thread_handle=::CreateThread(nullptr,0,ui_thread,nullptr,0,nullptr);
    if(!thread_handle) {
        DWORD error=::GetLastError();
        ::CloseHandle(ready_event);
        ready_event=nullptr;
        ::InterlockedExchange(&phase,failed);
        ::ReleaseSRWLockExclusive(&gate);
        return error;
    }
    HANDLE signal=ready_event;
    ::ReleaseSRWLockExclusive(&gate);
    if(::WaitForSingleObject(signal,5000)!=WAIT_OBJECT_0 || read(&phase)!=live)
        return ERROR_GEN_FAILURE; // On failure, do NOT allow FreeLibrary.
    return ERROR_SUCCESS;
}
extern "C" __declspec(dllexport) DWORD WINAPI OpenATLUnloadSubmit() {
    ::AcquireSRWLockShared(&gate);
    if(read(&phase)!=live || !live_window) {
        ::ReleaseSRWLockShared(&gate);
        return ERROR_BUSY;
    }
    BOOL posted=::PostMessageW(live_window,work_message,0,0);
    DWORD status=posted ? ERROR_SUCCESS : ::GetLastError();
    if(posted) ::InterlockedIncrement(&accepted);
    ::ReleaseSRWLockShared(&gate);
    return status;
}
extern "C" __declspec(dllexport) DWORD WINAPI OpenATLUnloadStop() {
    ::AcquireSRWLockExclusive(&gate);
    if(read(&phase)!=live || !thread_handle || !live_window) {
        ::ReleaseSRWLockExclusive(&gate);
        return ERROR_BUSY;
    }
    ::InterlockedExchange(&phase,closing);
    BOOL posted=::PostMessageW(live_window,stop_message,0,0);
    DWORD status=posted ? ERROR_SUCCESS : ::GetLastError();
    HANDLE owner=thread_handle;
    ::ReleaseSRWLockExclusive(&gate);
    if(!posted) return status; // No unsafe thread termination fallback.
    if(::WaitForSingleObject(owner,5000)!=WAIT_OBJECT_0)
        return WAIT_TIMEOUT;
    DWORD exit_code=0;
    if(!::GetExitCodeThread(owner,&exit_code) || exit_code!=0)
        return ERROR_GEN_FAILURE;
    ::AcquireSRWLockExclusive(&gate);
    bool clean=read(&accepted)==read(&processed) && read(&callbacks)==0 &&
               read(&finalized)==1 && read(&destroyed)==1 &&
               read(&released)==1 && read(&unregistered)==1 &&
               live_window==nullptr;
    ::CloseHandle(thread_handle);
    ::CloseHandle(ready_event);
    thread_handle=nullptr;
    ready_event=nullptr;
    ::InterlockedExchange(&phase,clean?quiesced:failed);
    ::ReleaseSRWLockExclusive(&gate);
    return clean?ERROR_SUCCESS:ERROR_INVALID_DATA;
}
extern "C" __declspec(dllexport) DWORD WINAPI OpenATLUnloadCanUnload() {
    ::AcquireSRWLockShared(&gate);
    bool ready=read(&phase)==quiesced &&
               thread_handle==nullptr && ready_event==nullptr &&
               live_window==nullptr && read(&callbacks)==0 &&
               read(&accepted)==read(&processed) &&
               read(&destroyed)==1 && read(&finalized)==1 &&
               read(&released)==1 && read(&unregistered)==1;
    ::ReleaseSRWLockShared(&gate);
    return ready?ERROR_SUCCESS:ERROR_BUSY;
}
extern "C" __declspec(dllexport) DWORD WINAPI OpenATLUnloadCount() {
    return static_cast<DWORD>(read(&processed));
}
