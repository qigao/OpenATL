// x64 CET user shadow-stack enforcement qualification.
//
// This file distinguishes CETCOMPAT in PE metadata from an OS/CPU-enforced
// user shadow stack. A supported host may execute a deliberately corrupted
// return ONLY in a separate Windows child process. Never suppress CET.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <intrin.h>
#include <atlbase.h>
#include <atlwin.h>
#include <cstdio>
#include <cstring>
#include <string>

#if !defined(OPENATL_REPOSITORY_HEADERS) || OPENATL_REPOSITORY_HEADERS != 0x4F41544CUL
#error "CET evidence must compile repository-owned OpenATL headers"
#endif
#if !defined(_M_X64) && !defined(__x86_64__)
#error "User shadow stack proof is x64-only; ARM64 is not CETCOMPAT"
#endif
static_assert(sizeof(void*) == 8, "CET x64 native acceptance requires 64-bit Windows");

using QueryMitigation = BOOL(WINAPI*)(HANDLE, int, void*, SIZE_T);
struct ShadowPolicy {
    DWORD flags = 0;
    DWORD error = 0;
    bool queried = false;
    bool enabled() const noexcept { return queried && (flags & 1u) != 0; }
    bool audit() const noexcept { return queried && (flags & 2u) != 0; }
    bool strict() const noexcept { return queried && (flags & (1u << 4)) != 0; }
};
static ShadowPolicy shadow_policy() noexcept {
    ShadowPolicy result;
    HMODULE kernel = ::GetModuleHandleW(L"kernel32.dll");
    if (!kernel) {
        result.error = ::GetLastError();
        return result;
    }
    auto query = reinterpret_cast<QueryMitigation>(
        ::GetProcAddress(kernel, "GetProcessMitigationPolicy"));
    if (!query) {
        result.error = ::GetLastError();
        return result;
    }
    result.queried = query(::GetCurrentProcess(),
                           15 /* ProcessUserShadowStackPolicy */,
                           &result.flags, sizeof(result.flags)) != FALSE;
    if (!result.queried)
        result.error = ::GetLastError();
    return result;
}

static void* saved_return = nullptr;
static volatile LONG observed_stages = 0;

// Chromium's CET regression also modifies the saved return address by this
// documented intrinsic. Both call sites must remain distinct and noinline.
// On enforcement the second return must cause an OS control-protection fault.
__declspec(noinline) static void corrupt_one_return() {
    // MSVC uses the native intrinsic. OpenATL supplies an _MSC_VER shim
    // for WTL on llvm-mingw, so test that marker explicitly. Clang's
    // __builtin_frame_address(0) on Win64 does NOT directly address the
    // saved return slot; the preserved RBP register does, at [RBP+8].
#if defined(_MSC_VER) && !defined(_OPENATL_DEFINED_MSC_VER)
    void* volatile* slot =
        reinterpret_cast<void* volatile*>(_AddressOfReturnAddress());
#else
    void* volatile* slot = nullptr;
    __asm__ volatile("leaq 8(%%rbp), %0" : "=r"(slot));
#endif
    if (saved_return == nullptr)
        saved_return = *slot;
    else if (*slot != saved_return)
        *slot = saved_return;
    else
        ::ExitProcess(84);  // Invalid fixture: the return sites are identical.
}
__declspec(noinline) static void return_site_a() {
    corrupt_one_return();
    ::InterlockedIncrement(&observed_stages);
}
__declspec(noinline) static void return_site_b() {
    corrupt_one_return();
    // Different post-call instruction from site A: link.exe /OPT:ICF
    // otherwise folds their identical function bodies, yielding the
    // same saved return address and a false-negative CET death probe.
    ::InterlockedExchangeAdd(&observed_stages, 2);
}

static int child(const char* option) {
    const ShadowPolicy policy = shadow_policy();
    if (!policy.enabled() || policy.audit())
        return 80;  // Refuse to call the dangerous regression unprotected.
    ::SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
    return_site_a();
    if (observed_stages != 1 || saved_return == nullptr)
        return 81;
    if (std::strcmp(option, "--control") == 0)
        return 0;
    return_site_b();
    // Returning normally would mean the OS did not enforce the mismatch.
    return 82;
}

static bool spawn(const wchar_t* option, DWORD* exitCode) {
    wchar_t executable[MAX_PATH] = {};
    const DWORD length = ::GetModuleFileNameW(nullptr, executable, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return false;
    std::wstring args = L"\"";
    args += executable;
    args += L"\" ";
    args += option;
    STARTUPINFOW start{};
    start.cb = sizeof(start);
    PROCESS_INFORMATION process{};
    if (!::CreateProcessW(executable, &args[0], nullptr, nullptr, FALSE,
                          CREATE_NO_WINDOW | CREATE_DEFAULT_ERROR_MODE,
                          nullptr, nullptr, &start, &process))
        return false;
    (void)::CloseHandle(process.hThread);
    const DWORD wait = ::WaitForSingleObject(process.hProcess, 7000);
    if (wait != WAIT_OBJECT_0) {
        if (wait == WAIT_TIMEOUT) {
            (void)::TerminateProcess(process.hProcess, 99);
            (void)::WaitForSingleObject(process.hProcess, 1000);
        }
        (void)::CloseHandle(process.hProcess);
        return false;
    }
    const BOOL good = ::GetExitCodeProcess(process.hProcess, exitCode);
    (void)::CloseHandle(process.hProcess);
    return good != FALSE;
}

// Capture a Win32 fail-fast's *subcode* via a real debug event, not just
// the ambiguous 0xC0000409 process exit. A generic stack cookie or CFG
// failure must never count as a hardware CET return-address violation.
struct CrashEvidence {
    DWORD processExit = ~0u;
    DWORD exception = 0;
    ULONG_PTR fastFailCode = ~static_cast<ULONG_PTR>(0);
    bool sawFastFail = false;
};
static bool spawn_debug_violation(CrashEvidence* evidence) {
    if (!evidence)
        return false;
    wchar_t executable[MAX_PATH] = {};
    const DWORD len = ::GetModuleFileNameW(nullptr, executable, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
        return false;
    std::wstring command = L"\"";
    command += executable;
    command += L"\" --corrupt-return";
    STARTUPINFOW start{};
    start.cb = sizeof(start);
    PROCESS_INFORMATION childProcess{};
    if (!::CreateProcessW(executable, &command[0], nullptr, nullptr, FALSE,
                          DEBUG_ONLY_THIS_PROCESS | CREATE_NO_WINDOW |
                              CREATE_DEFAULT_ERROR_MODE,
                          nullptr, nullptr, &start, &childProcess))
        return false;
    (void)::CloseHandle(childProcess.hThread);
    const ULONGLONG deadline = ::GetTickCount64() + 7000;
    bool completed = false;
    bool error = false;
    while (::GetTickCount64() < deadline) {
        const DWORD remaining =
            static_cast<DWORD>(deadline - ::GetTickCount64());
        DEBUG_EVENT event{};
        if (!::WaitForDebugEvent(&event, remaining)) {
            error = true;
            break;
        }
        DWORD decision = DBG_CONTINUE;
        const bool done = event.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT;
        switch (event.dwDebugEventCode) {
        case CREATE_PROCESS_DEBUG_EVENT:
            if (event.u.CreateProcessInfo.hFile)
                (void)::CloseHandle(event.u.CreateProcessInfo.hFile);
            break;
        case LOAD_DLL_DEBUG_EVENT:
            if (event.u.LoadDll.hFile)
                (void)::CloseHandle(event.u.LoadDll.hFile);
            break;
        case EXCEPTION_DEBUG_EVENT: {
            const auto& record = event.u.Exception.ExceptionRecord;
            if (record.ExceptionCode == 0xC0000409u &&
                record.NumberParameters >= 1) {
                evidence->exception = record.ExceptionCode;
                evidence->fastFailCode = record.ExceptionInformation[0];
                evidence->sawFastFail = true;
            }
            // The OS's initial debugger breakpoint must be handled.
            // Real exception signals must continue unhandled so Windows
            // terminates the isolated child by its own mitigation policy.
            decision = record.ExceptionCode == EXCEPTION_BREAKPOINT ?
                DBG_CONTINUE : DBG_EXCEPTION_NOT_HANDLED;
            break;
        }
        case EXIT_PROCESS_DEBUG_EVENT:
            evidence->processExit = event.u.ExitProcess.dwExitCode;
            completed = true;
            break;
        default:
            break;
        }
        if (!::ContinueDebugEvent(event.dwProcessId,
                                  event.dwThreadId, decision)) {
            error = true;
            break;
        }
        if (done)
            break;
    }
    if (!completed) {
        // This is the *only* process that may be terminated by the test.
        (void)::TerminateProcess(childProcess.hProcess, 99);
        (void)::WaitForSingleObject(childProcess.hProcess, 1000);
    }
    (void)::CloseHandle(childProcess.hProcess);
    return completed && !error;
}

// A CET-enabled return-address death test alone does not qualify OpenATL's
// generated executable WndProc. Exercise the actual OpenATL CWndProcThunk
// indirect call under that same enforced shadow-stack process policy.
static LRESULT CALLBACK cet_window_thunk(HWND opaque, UINT message,
                                         WPARAM w, LPARAM l) {
    if (message != WM_APP + 91 || opaque == nullptr)
        return -91;
    int* invocations = reinterpret_cast<int*>(opaque);
    ++*invocations;
    return static_cast<LRESULT>(w + l);
}

static bool qualify_cet_thunk() {
    int invocations = 0;
    void* code = nullptr;
    {
        ATL::CWndProcThunk thunk;
        if (!thunk.Init(cet_window_thunk, &invocations))
            return false;
        code = thunk.GetCodeAddress();
        MEMORY_BASIC_INFORMATION page{};
        if (!code || ::VirtualQuery(code, &page, sizeof(page)) !=
                         sizeof(page) ||
            page.State != MEM_COMMIT ||
            (page.Protect & 0xffu) != PAGE_EXECUTE_READ ||
            (page.Protect & PAGE_GUARD) != 0)
            return false;
        const WNDPROC callback = thunk.GetWNDPROC();
        if (!callback || callback(nullptr, WM_APP + 91, 17, 25) != 42 ||
            invocations != 1)
            return false;
    }
    // The thunk owner is destroyed only after its callback returned.
    MEMORY_BASIC_INFORMATION page{};
    return code && ::VirtualQuery(code, &page, sizeof(page)) ==
                       sizeof(page) && page.State == MEM_FREE;
}

static int qualify(bool required) {
    ShadowPolicy policy = shadow_policy();
    if (!policy.enabled() || policy.audit()) {
        std::printf("OPENATL_CET_UNQUALIFIED query=%d enabled=%d audit=%d "
                    "strict=%d error=%lu\n",
                    policy.queried ? 1 : 0, policy.enabled() ? 1 : 0,
                    policy.audit() ? 1 : 0, policy.strict() ? 1 : 0,
                    static_cast<unsigned long>(policy.error));
        // The ordinary CFG/PE CI only inventories CET. An explicitly
        // requested CET *enforcement* gate MUST fail when it is unavailable.
        return required ? 77 : 0;
    }
    if (!qualify_cet_thunk()) {
        std::fprintf(stderr, "OPENATL_CET_FAIL stage=HSP_protected_thunk\n");
        return 93;
    }
    DWORD controlExit = ~0u;
    if (!spawn(L"--control", &controlExit) || controlExit != 0) {
        std::fprintf(stderr,
                     "OPENATL_CET_FAIL stage=control exit=0x%08lx\n",
                     static_cast<unsigned long>(controlExit));
        return 90;
    }
    CrashEvidence evidence;
    if (!spawn_debug_violation(&evidence)) {
        std::fprintf(stderr, "OPENATL_CET_FAIL stage=debug_violation_probe\n");
        return 91;
    }
    // FAST_FAIL_CONTROL_INVALID_RETURN_ADDRESS = 0x39. An unqualified
    // process exit code is insufficient: 0xC0000409 can also indicate
    // stack cookies and CFG errors. Require an OS debug exception record.
    constexpr ULONG_PTR invalidReturnAddress = 0x39;
    if (!evidence.sawFastFail ||
        evidence.exception != 0xC0000409u ||
        evidence.processExit != 0xC0000409u ||
        evidence.fastFailCode != invalidReturnAddress) {
        std::fprintf(stderr,
            "OPENATL_CET_FAIL stage=wrong_fastfail exit=0x%08lx "
            "exception=0x%08lx subcode=0x%llx observed=%d\n",
            static_cast<unsigned long>(evidence.processExit),
            static_cast<unsigned long>(evidence.exception),
            static_cast<unsigned long long>(evidence.fastFailCode),
            evidence.sawFastFail ? 1 : 0);
        return 92;
    }
    std::printf("OPENATL_CET_ENFORCED_OK query=1 enabled=1 audit=0 "
                "strict=%d control=0x00000000 "
                "exception=0x%08lx fastfail=0x%llx thunk=1\n",
                policy.strict() ? 1 : 0,
                static_cast<unsigned long>(evidence.exception),
                static_cast<unsigned long long>(evidence.fastFailCode));
    return 0;
}

int main(int argc, char** argv) {
    if (argc != 2)
        return 2;
    if (std::strcmp(argv[1], "--inventory") == 0)
        return qualify(false);
    if (std::strcmp(argv[1], "--require-hardware") == 0)
        return qualify(true);
    if (std::strcmp(argv[1], "--control") == 0 ||
        std::strcmp(argv[1], "--corrupt-return") == 0)
        return child(argv[1]);
    return 3;
}
