// Windows x64/ARM64 native concurrent LoadLibrary/FreeLibrary acceptance.
// Never unload with executing DLL callbacks or producer exports on stack.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <objbase.h>
#include <atlbase.h>
#include <cstdio>
#include <cwchar>

#if !defined(OPENATL_REPOSITORY_HEADERS) || OPENATL_REPOSITORY_HEADERS != 0x4F41544CUL
#error "Use source-owned OpenATL headers"
#endif
using Operation=DWORD (WINAPI*)();

struct Producer {
    Operation submit=nullptr;
    DWORD successes=0, busy=0, failure=0;
};
struct Stopper {
    Operation stop=nullptr;
    DWORD status=ERROR_BUSY;
};
DWORD WINAPI stop_owner(void* arg) {
    Stopper& item=*static_cast<Stopper*>(arg);
    item.status=item.stop();
    return item.status==ERROR_SUCCESS ? 0 : 1;
}
DWORD WINAPI send_messages(void* arg) {
    Producer& producer=*static_cast<Producer*>(arg);
    const ULONGLONG end=::GetTickCount64()+2500;
    for(;;) {
        if(::GetTickCount64()>end) {
            producer.failure=WAIT_TIMEOUT;
            return 1;
        }
        DWORD ret=producer.submit();
        if(ret==ERROR_BUSY) {
            ++producer.busy;
            return 0;
        }
        if(ret!=ERROR_SUCCESS) {
            producer.failure=ret;
            return 2;
        }
        ++producer.successes;
        ::Sleep(1);
    }
}
static int fail(int round,const char* stage,DWORD status) {
    std::fprintf(stderr,
        "OPENATL_UNLOAD_FAIL round=%d stage=%s code=%lu last=%lu\n",
        round,stage,static_cast<unsigned long>(status),
        static_cast<unsigned long>(::GetLastError()));
    return 1;
}
int main() {
    ULONG_PTR token=0;
    if(::CoGetContextToken(&token)!=CO_E_NOTINITIALIZED)
        return fail(0,"COM_initialized",ERROR_INVALID_STATE);
    wchar_t dll[MAX_PATH]={};
    DWORD count=::GetModuleFileNameW(nullptr,dll,MAX_PATH);
    if(count==0 || count>=MAX_PATH)
        return fail(0,"GetModuleFileName",::GetLastError());
    wchar_t* slash=std::wcsrchr(dll,L'\\');
    if(!slash)
        return fail(0,"DLL_directory",ERROR_INVALID_NAME);
    constexpr wchar_t filename[]=L"openatl_unload_fixture.dll";
    if(static_cast<size_t>(MAX_PATH-(slash+1-dll)) <
       sizeof(filename)/sizeof(filename[0]))
        return fail(0,"DLL_path_capacity",ERROR_BUFFER_OVERFLOW);
    std::wcscpy(slash+1,filename);

    constexpr int rounds=12, nProducers=4;
    for(int round=0;round<rounds;++round) {
        HMODULE dllModule=::LoadLibraryW(dll);
        if(!dllModule)
            return fail(round,"LoadLibrary",::GetLastError());
        auto fn=[dllModule](const char* name)->Operation {
            return reinterpret_cast<Operation>(
                ::GetProcAddress(dllModule,name));
        };
        Operation start=fn("OpenATLUnloadStart");
        Operation submit=fn("OpenATLUnloadSubmit");
        Operation stop=fn("OpenATLUnloadStop");
        Operation canUnload=fn("OpenATLUnloadCanUnload");
        Operation processed=fn("OpenATLUnloadCount");
        Operation retireCallers=fn("OpenATLUnloadRetireCallers");
        Operation acquireLease=fn("OpenATLUnloadAcquireCallerLease");
        Operation releaseLease=fn("OpenATLUnloadReleaseCallerLease");
        Operation hold=fn("OpenATLUnloadHold");
        Operation waitHeld=fn("OpenATLUnloadWaitHeld");
        Operation releaseHeld=fn("OpenATLUnloadReleaseHeld");
        Operation isClosing=fn("OpenATLUnloadIsClosing");
        if(!start || !submit || !stop || !canUnload || !processed ||
           !retireCallers || !acquireLease || !releaseLease ||
           !hold || !waitHeld || !releaseHeld || !isClosing)
            return fail(round,"exports",ERROR_PROC_NOT_FOUND);
        if(start()!=ERROR_SUCCESS)
            return fail(round,"create_owner_thread",ERROR_GEN_FAILURE);
        if(canUnload()!=ERROR_BUSY || retireCallers()!=ERROR_BUSY ||
           releaseLease()!=ERROR_INVALID_PARAMETER)
            return fail(round,"early_unload_admitted",ERROR_INVALID_DATA);
        // Exercise lease saturation explicitly, including the first rejected
        // acquire and an extra release. This must not change the number
        // of leases visible to the later STOP/RetireCallers handshake.
        if(round==0) {
            for(int i=0;i<1024;++i)
                if(acquireLease()!=ERROR_SUCCESS)
                    return fail(round,"lease_capacity_admit",ERROR_GEN_FAILURE);
            if(acquireLease()!=ERROR_NOT_ENOUGH_QUOTA)
                return fail(round,"lease_capacity_overflow",ERROR_INVALID_DATA);
            for(int i=0;i<1024;++i)
                if(releaseLease()!=ERROR_SUCCESS)
                    return fail(round,"lease_capacity_drain",ERROR_GEN_FAILURE);
            if(releaseLease()!=ERROR_INVALID_PARAMETER)
                return fail(round,"lease_capacity_underflow",ERROR_INVALID_DATA);
        }
        for(int i=0;i<32;++i)
            if(submit()!=ERROR_SUCCESS)
                return fail(round,"seed_message",ERROR_GEN_FAILURE);
        // Force an actual active WndProc callback before stop races with
        // producers. This callback cannot complete until the host releases it.
        if(hold()!=ERROR_SUCCESS || waitHeld()!=ERROR_SUCCESS)
            return fail(round,"held_callback_admission",ERROR_GEN_FAILURE);
        if(canUnload()!=ERROR_BUSY)
            return fail(round,"held_callback_cannot_unload",ERROR_INVALID_DATA);

        Producer producers[nProducers]{};
        HANDLE threads[nProducers]{};
        // Claim one owner-visible lease for each potential DLL caller.
        // The host only releases it AFTER joining that exact producer.
        for(int i=0;i<nProducers;++i)
            if(acquireLease()!=ERROR_SUCCESS)
                return fail(round,"acquire_producer_lease",ERROR_GEN_FAILURE);
        for(int i=0;i<nProducers;++i) {
            producers[i].submit=submit;
            threads[i]=::CreateThread(nullptr,0,send_messages,
                                       &producers[i],0,nullptr);
            if(!threads[i])
                return fail(round,"CreateThread",::GetLastError());
        }
        ::Sleep(15);
        Stopper stopper{stop,ERROR_BUSY};
        HANDLE stopperThread=::CreateThread(nullptr,0,stop_owner,
                                             &stopper,0,nullptr);
        if(!stopperThread)
            return fail(round,"create_stop_thread",::GetLastError());
        // Admission must close even while the current callback is blocked.
        const ULONGLONG admissionEnd=::GetTickCount64()+1000;
        while(isClosing()!=ERROR_SUCCESS &&
              ::GetTickCount64()<admissionEnd)
            ::Sleep(1);
        if(isClosing()!=ERROR_SUCCESS || canUnload()!=ERROR_BUSY ||
           retireCallers()!=ERROR_BUSY ||
           acquireLease()!=ERROR_BUSY ||
           ::WaitForSingleObject(stopperThread,25)!=WAIT_TIMEOUT)
            return fail(round,"blocked_callback_close_fence",ERROR_INVALID_DATA);
        if(releaseHeld()!=ERROR_SUCCESS)
            return fail(round,"release_held_callback",ERROR_GEN_FAILURE);
        DWORD joined=::WaitForSingleObject(stopperThread,5000);
        DWORD stopExitCode=~0u;
        bool stopped=joined==WAIT_OBJECT_0 &&
                     ::GetExitCodeThread(stopperThread,&stopExitCode) &&
                     stopExitCode==0 && stopper.status==ERROR_SUCCESS;
        ::CloseHandle(stopperThread);
        if(!stopped)
            return fail(round,"owner_drain_and_join",stopper.status);
        // STOP drained the DLL owner but the host still has producer threads
        // that may have entered an export; unload MUST remain inadmissible.
        if(canUnload()!=ERROR_BUSY || submit()!=ERROR_BUSY ||
           retireCallers()!=ERROR_BUSY || acquireLease()!=ERROR_BUSY)
            return fail(round,"owner_done_but_callers_live",ERROR_INVALID_DATA);
        DWORD accepted=33; // 32 ordinary posts + one held callback
        for(int i=0;i<nProducers;++i) {
            DWORD result=::WaitForSingleObject(threads[i],5000);
            DWORD exitCode=~0u;
            bool ok=result==WAIT_OBJECT_0 &&
                    ::GetExitCodeThread(threads[i],&exitCode) &&
                    exitCode==0 && producers[i].failure==0 &&
                    producers[i].busy==1;
            ::CloseHandle(threads[i]);
            if(!ok)
                return fail(round,"submission_overlap",producers[i].failure);
            accepted+=producers[i].successes;
            if(releaseLease()!=ERROR_SUCCESS)
                return fail(round,"release_joined_producer_lease",ERROR_GEN_FAILURE);
        }
        if(processed()!=accepted)
            return fail(round,"accepted_messages_lost",ERROR_INVALID_DATA);
        // Only the host can prove every thread that could invoke the DLL
        // exports has exited. Publish that proof after joining them all.
        if(retireCallers()!=ERROR_SUCCESS || canUnload()!=ERROR_SUCCESS ||
           releaseLease()!=ERROR_INVALID_PARAMETER)
            return fail(round,"retire_callers_before_FreeLibrary",
                        ERROR_INVALID_DATA);
        if(!::FreeLibrary(dllModule))
            return fail(round,"FreeLibrary",::GetLastError());
        if(::GetModuleHandleW(filename))
            return fail(round,"stale_loaded_module",ERROR_BUSY);
        if(::CoGetContextToken(&token)!=CO_E_NOTINITIALIZED)
            return fail(round,"COM_initialized_after_unload",
                        ERROR_INVALID_STATE);
    }
    std::puts("OPENATL_UNLOAD_OK rounds=12 events=1 lease=1 held=1 blocked=1 callers_joined=1 caller_leases=1 lease_bounds=1 thunk_freed=1 class_unregistered=1 dll_unmapped=1 no_com=1");
    return 0;
}
