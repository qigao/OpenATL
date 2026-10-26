/* Fault admission for pinned, test-only OpenATL CWndProcThunk W^X patch.
 * Each test performs REAL Windows VirtualAlloc, VirtualProtect, Flush and
 * VirtualFree calls except for the exact single stage intentionally denied.
 * No COM apartment, app-owned COM class, RATL or shipped API involved.
 */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef OPENATL_WX_FAULT_TEST
#error "Refuse thunk fault test without isolated exact-pin instrumentation"
#endif
#include <windows.h>
#include <cstdio>
#include <atlwin.h>

static_assert(sizeof(void*)==8, "Native Windows x64/ARM64 required");
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
int main() {
    for(int i=0;i<24;++i) {
        int status=fault_new(1,10); if(status)return status;
        status=fault_new(3,20); if(status)return status;
        status=fault_new(4,30); if(status)return status;
        status=fault_reinit_rw(); if(status)return status;
        status=recovered_reinit(); if(status)return status;
    }
    std::puts("THUNK_FAULT_OK alloc=1 rx=1 flush=1 reinit_rw=1 free=1 recovery=1 no_rwx=1");
    return 0;
}
