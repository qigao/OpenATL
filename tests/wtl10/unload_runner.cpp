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
static_assert(sizeof(void*)==8,"Native x64/ARM64 required");
using Operation=DWORD (WINAPI*)();

struct Producer {
    Operation submit=nullptr;
    DWORD successes=0, busy=0, failure=0;
};
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
        if(!start || !submit || !stop || !canUnload || !processed)
            return fail(round,"exports",ERROR_PROC_NOT_FOUND);
        if(start()!=ERROR_SUCCESS)
            return fail(round,"create_owner_thread",ERROR_GEN_FAILURE);
        if(canUnload()!=ERROR_BUSY)
            return fail(round,"early_unload_admitted",ERROR_INVALID_DATA);
        for(int i=0;i<32;++i)
            if(submit()!=ERROR_SUCCESS)
                return fail(round,"seed_message",ERROR_GEN_FAILURE);

        Producer producers[nProducers]{};
        HANDLE threads[nProducers]{};
        for(int i=0;i<nProducers;++i) {
            producers[i].submit=submit;
            threads[i]=::CreateThread(nullptr,0,send_messages,
                                       &producers[i],0,nullptr);
            if(!threads[i])
                return fail(round,"CreateThread",::GetLastError());
        }
        ::Sleep(15);
        if(stop()!=ERROR_SUCCESS)
            return fail(round,"owner_drain_and_join",ERROR_GEN_FAILURE);
        if(canUnload()!=ERROR_SUCCESS || submit()!=ERROR_BUSY)
            return fail(round,"unload_gate",ERROR_INVALID_DATA);
        DWORD accepted=32;
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
        }
        if(processed()!=accepted)
            return fail(round,"accepted_messages_lost",ERROR_INVALID_DATA);
        if(!::FreeLibrary(dllModule))
            return fail(round,"FreeLibrary",::GetLastError());
        if(::GetModuleHandleW(filename))
            return fail(round,"stale_loaded_module",ERROR_BUSY);
        if(::CoGetContextToken(&token)!=CO_E_NOTINITIALIZED)
            return fail(round,"COM_initialized_after_unload",
                        ERROR_INVALID_STATE);
    }
    std::puts("OPENATL_UNLOAD_OK rounds=12 events=1 lease=1 thunk_freed=1 class_unregistered=1 dll_unmapped=1 no_com=1");
    return 0;
}
