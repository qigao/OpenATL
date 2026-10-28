/* Fault admission for the in-tree OpenATL CWndProcThunk W^X implementation.
 * Each test performs REAL Windows VirtualAlloc, VirtualProtect, Flush and
 * VirtualFree calls except for the exact single stage intentionally denied.
 * No COM apartment, app-owned COM class, RATL or shipped API involved.
 */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef OPENATL_WX_FAULT_TEST
#error "Refuse thunk fault test without isolated in-tree instrumentation"
#endif
#include <windows.h>
#include <cstdio>
#include <atlwin.h>

#if !defined(OPENATL_REPOSITORY_HEADERS) || OPENATL_REPOSITORY_HEADERS != 0x4F41544CUL
#error "This target must compile repository-owned OpenATL headers"
#endif

using Thunk=ATL::CWndProcThunk;
static int calls=0;
static void* expected_state=nullptr;

static LRESULT CALLBACK dispatch(HWND owner, UINT msg, WPARAM w, LPARAM l) {
    if((void*)owner!=expected_state || msg!=WM_APP+77)
        return -77;
    ++calls;
    return (LRESULT)(w+l);
}
static int reject(int code, const char* step) {
    std::fprintf(stderr,
        "THUNK_FAULT_FAIL code=%d step=%s live=%d injected=%d fault=%d\n",
        code,step,Thunk::testLivePages,Thunk::testInjectedCount,
        Thunk::testFailureStage);
    return code;
}
static bool no_executable_page(void* addr) {
    if(addr==nullptr)return true;
    MEMORY_BASIC_INFORMATION mbi={};
    if(::VirtualQuery(addr,&mbi,sizeof(mbi))!=sizeof(mbi))
        return false;
    return mbi.State==MEM_FREE;
}
static bool rx_only(void* addr) {
    if(addr==nullptr)return false;
    MEMORY_BASIC_INFORMATION mbi={};
    return ::VirtualQuery(addr,&mbi,sizeof(mbi))==sizeof(mbi) &&
        mbi.State==MEM_COMMIT &&
        (mbi.Protect & 0xFFu)==PAGE_EXECUTE_READ &&
        (mbi.Protect & PAGE_GUARD)==0;
}
static int fault_new(int stage,int id) {
    Thunk::TestFailNext(stage);
    int state=0;
    expected_state=&state;
    {
        Thunk instance;
        if(instance.Init(dispatch,&state) ||
           instance.GetCodeAddress()!=nullptr ||
           instance.GetWNDPROC()!=nullptr ||
           Thunk::testInjectedCount!=1 ||
           Thunk::testFailureStage!=0 ||
           Thunk::testLivePages!=0)
            return reject(id,"new_thunk_did_not_fail_closed");
        if(stage==1) {
            if(Thunk::testLastAllocated!=nullptr ||
               Thunk::testLastReleased!=nullptr)
                return reject(id+1,"alloc_error_leaked_page");
        } else {
            if(Thunk::testLastAllocated==nullptr ||
               Thunk::testLastAllocated!=Thunk::testLastReleased ||
               !no_executable_page(Thunk::testLastReleased))
                return reject(id+2,"failed_init_memory_not_released");
        }
    }
    if(Thunk::testLivePages!=0)
        return reject(id+3,"destructor_leaked_page");
    return 0;
}
static int fault_reinit_rw() {
    Thunk::TestFailNext(0);
    int state=0;
    expected_state=&state;
    {
        Thunk thunk;
        if(!thunk.Init(dispatch,&state) || !rx_only(thunk.GetCodeAddress()) ||
           Thunk::testLivePages!=1)
            return reject(50,"precondition_initial_RX");
        void* previous=thunk.GetCodeAddress();
        Thunk::TestFailNext(2);
        if(thunk.Init(dispatch,&state) ||
           thunk.GetCodeAddress()!=nullptr ||
           thunk.GetWNDPROC()!=nullptr ||
           Thunk::testLivePages!=0 ||
           Thunk::testInjectedCount!=1 ||
           Thunk::testLastReleased!=previous ||
           !no_executable_page(previous))
            return reject(51,"reinit_denial_kept_callable_thunk");
    }
    return Thunk::testLivePages==0?0:reject(52,"reinit_destructor_leak");
}
static int recovered_reinit() {
    Thunk::TestFailNext(0);
    int state=0;
    expected_state=&state;
    int before=calls;
    {
        Thunk thunk;
        if(!thunk.Init(dispatch,&state) || !rx_only(thunk.GetCodeAddress()) ||
           !thunk.Init(dispatch,&state) || !rx_only(thunk.GetCodeAddress()))
            return reject(60,"good_reinit_did_not_commit_RX");
        WNDPROC proc=thunk.GetWNDPROC();
        if(proc==nullptr || proc(nullptr,WM_APP+77,17,25)!=42 ||
           calls!=before+1 || !rx_only(thunk.GetCodeAddress()) ||
           Thunk::testLivePages!=1)
            return reject(61,"post_fault_recovery_execution");
    }
    if(Thunk::testLivePages!=0 ||
       Thunk::testLastReleased==nullptr ||
       !no_executable_page(Thunk::testLastReleased))
        return reject(62,"recovered_thunk_not_released");
    return 0;
}
// Exercise failures at the real ATL window and dialog creation sites,
// not only CWndProcThunk::Init directly. In each case Create must refuse
// admission before publishing a HWND/window procedure.
class FailedWindow : public ATL::CWindowImpl<FailedWindow> {
public:
    DECLARE_WND_CLASS(L"OpenATL_FailedWndProcThunk")
    BEGIN_MSG_MAP(FailedWindow)
    END_MSG_MAP()
};
class FailedDialog : public ATL::CDialogImpl<FailedDialog> {
public:
    enum { IDD = 1099 }; // Not opened: failure must occur before reading RC.
    BEGIN_MSG_MAP(FailedDialog)
    END_MSG_MAP()
};
static int fault_window_or_dialog(int stage,bool dialog) {
    Thunk::TestFailNext(stage);
    HWND admitted=nullptr;
    bool ownerClean=false;
    if(dialog) {
        FailedDialog candidate;
        admitted=candidate.Create(NULL);
        ownerClean=(candidate.m_hWnd==nullptr &&
                    candidate.m_thunk.GetCodeAddress()==nullptr);
        if(admitted!=nullptr)::DestroyWindow(admitted);
    } else {
        FailedWindow candidate;
        RECT bounds={0,0,80,40};
        admitted=candidate.Create(NULL,bounds,L"Fault admission",WS_POPUP,0);
        ownerClean=(candidate.m_hWnd==nullptr &&
                    candidate.m_thunk.GetCodeAddress()==nullptr);
        if(admitted!=nullptr)::DestroyWindow(admitted);
    }
    if(admitted!=nullptr || !ownerClean || Thunk::testInjectedCount!=1 ||
       Thunk::testFailureStage!=0 || Thunk::testLivePages!=0)
        return reject(dialog?80:70,"window_or_dialog_published_on_thunk_failure");
    if(stage==1) {
        if(Thunk::testLastAllocated!=nullptr ||
           Thunk::testLastReleased!=nullptr)
            return reject(81,"window_alloc_denial_leaked_memory");
    } else {
        if(Thunk::testLastAllocated==nullptr ||
           Thunk::testLastAllocated!=Thunk::testLastReleased ||
           !no_executable_page(Thunk::testLastReleased))
            return reject(82,"window_failed_thunk_page_retained");
    }
    return 0;
}


// A CreateWindowEx/CreateDialogParam failure may happen before Win32 ever
// invokes the Start*Proc. Those stack-local pending records must be revoked
// before a later window can consume them as an unrelated stale owner.
static int check_failed_creation_retirement() {
    if (ATL::_AtlWinModule.m_pCreateWndList != nullptr)
        return reject(90, "pending_creation_precondition");
    Thunk::TestFailNext(0);
    {
        FailedDialog missingResource;
        if (missingResource.Create(nullptr) != nullptr ||
            missingResource.m_hWnd != nullptr ||
            ATL::_AtlWinModule.m_pCreateWndList != nullptr)
            return reject(91, "missing_dialog_left_pending_create");
        if (missingResource.DoModal(nullptr) != -1 ||
            ATL::_AtlWinModule.m_pCreateWndList != nullptr)
            return reject(92, "missing_modal_left_pending_create");
    }
    const int stages[] = {5, 6};
    for (int stage : stages) {
        Thunk::TestFailNext(stage);
        {
            FailedWindow owner;
            RECT area{0, 0, 80, 40};
            HWND hwnd = owner.Create(nullptr, area, L"DeniedThunkWindow",
                                     WS_POPUP, 0);
            if (hwnd != nullptr || owner.m_hWnd != nullptr ||
                ATL::_AtlWinModule.m_pCreateWndList != nullptr ||
                Thunk::testFailureStage != 0 ||
                Thunk::testInjectedCount != 1)
                return reject(stage == 5 ? 93 : 94,
                              "failed_window_retained_owner_or_pending_data");
        }
        if (Thunk::testLivePages != 0)
            return reject(95, "failed_create_retained_exec_memory");
    }
    // Ensure no stale create stack node is accidentally consumed by a later
    // unrelated object after all early-failure modes.
    Thunk::TestFailNext(0);
    {
        FailedWindow fresh;
        RECT area{0,0,80,40};
        HWND hwnd=fresh.Create(nullptr,area,L"FreshOwner",WS_POPUP,0);
        if (!hwnd || !::IsWindow(hwnd) ||
            ATL::_AtlWinModule.m_pCreateWndList != nullptr ||
            !::DestroyWindow(hwnd) || ::IsWindow(hwnd))
            return reject(96, "new_window_consumed_stale_create_owner");
    }
    if (Thunk::testLivePages != 0)
        return reject(97, "fresh_window_page_not_released");
    return 0;
}


// A WM_CREATE handler is allowed to reject creation (-1), causing Win32
// to synchronously send WM_NCDESTROY and OnFinalMessage. That callback may
// delete the C++ owner before CreateWindowEx returns. No failed-return path
// can read the deleted owner again.
class SelfDeletingCreate : public ATL::CWindowImpl<SelfDeletingCreate> {
public:
    inline static int finalized=0;
    inline static int destroyed=0;
    DECLARE_WND_CLASS(L"OpenATL_SelfDeleting_Create")
    BEGIN_MSG_MAP(SelfDeletingCreate)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
    END_MSG_MAP()
    LRESULT OnCreate(UINT,WPARAM,LPARAM,BOOL&) { return -1; }
    void OnFinalMessage(HWND) override {
        ++finalized;
        delete this;
    }
    ~SelfDeletingCreate() { ++destroyed; }
};
static int check_self_deleting_create() {
    const int finalized=SelfDeletingCreate::finalized;
    const int destroyed=SelfDeletingCreate::destroyed;
    auto* owner=new SelfDeletingCreate();
    RECT area{0,0,90,50};
    HWND hwnd=owner->Create(nullptr,area,L"RejectedWin32Create",WS_POPUP,0);
    if (hwnd!=nullptr)
        return reject(98,"WM_CREATE_did_not_fail");
    if (SelfDeletingCreate::finalized!=finalized+1 ||
        SelfDeletingCreate::destroyed!=destroyed+1 ||
        ATL::_AtlWinModule.m_pCreateWndList!=nullptr ||
        Thunk::testLivePages!=0)
        return reject(99,"self_deleted_owner_retained_after_failed_create");
    return 0;
}

int main() {
    for(int i=0;i<24;++i) {
        int status=fault_new(1,10); if(status)return status;
        status=fault_new(3,20); if(status)return status;
        status=fault_new(4,30); if(status)return status;
        status=fault_reinit_rw(); if(status)return status;
        status=recovered_reinit(); if(status)return status;
        status=fault_window_or_dialog(1,false); if(status)return status;
        status=fault_window_or_dialog(3,false); if(status)return status;
        status=fault_window_or_dialog(4,false); if(status)return status;
        status=fault_window_or_dialog(1,true); if(status)return status;
        status=fault_window_or_dialog(3,true); if(status)return status;
        status=fault_window_or_dialog(4,true); if(status)return status;
    }
    if (int rc=check_failed_creation_retirement()) return rc;
    if (int rc=check_self_deleting_create()) return rc;
    std::puts("THUNK_FAULT_OK alloc=1 rx=1 flush=1 reinit_rw=1 free=1 recovery=1 no_rwx=1 window_denied=1 dialog_denied=1");
    return 0;
}
