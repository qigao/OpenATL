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
    // MSVC lowers this as an intrinsic. llvm-mingw declares but does not
    // lower the same name; use x64 [RBP+8] with a real frame pointer.
    // The LLVM target is compiled -fno-omit-frame-pointer and no sibling calls.
#if defined(_MSC_VER) && !defined(_OPENATL_DEFINED_MSC_VER)
    void* volatile* slot =
        reinterpret_cast<void* volatile*>(_AddressOfReturnAddress());
#else
    void* volatile* slot =
        reinterpret_cast<void* volatile*>(__builtin_frame_address(0)) + 1;
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
    DWORD controlExit = ~0u;
    if (!spawn(L"--control", &controlExit) || controlExit != 0) {
        std::fprintf(stderr,
                     "OPENATL_CET_FAIL stage=control exit=0x%08lx\n",
                     static_cast<unsigned long>(controlExit));
        return 90;
    }
    DWORD violationExit = ~0u;
    if (!spawn(L"--corrupt-return", &violationExit)) {
        std::fprintf(stderr, "OPENATL_CET_FAIL stage=violation_probe\n");
        return 91;
    }
    // Windows may surface control-protection faults as 0xC0000602 or
    // 0xC000060A. Other crashes (access violation, fast-fail, etc.) are
    // insufficient to prove shadow-stack enforcement.
    constexpr DWORD controlProtection = 0xC0000602u;
    constexpr DWORD controlProtectionAlt = 0xC000060Au;
    if (violationExit != controlProtection &&
        violationExit != controlProtectionAlt) {
        std::fprintf(stderr,
                     "OPENATL_CET_FAIL stage=wrong_exception exit=0x%08lx\n",
                     static_cast<unsigned long>(violationExit));
        return 92;
    }
    std::printf("OPENATL_CET_ENFORCED_OK query=1 enabled=1 audit=0 "
                "strict=%d control=0x00000000 exception=0x%08lx\n",
                policy.strict() ? 1 : 0,
                static_cast<unsigned long>(violationExit));
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
