/* Native CFG guarded-thunk and no-dynamic-code denial tests.
 * CET hardware shadow-stack state is recorded, never inferred from flags.
 */
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <atlwin.h>

#if !defined(OPENATL_REPOSITORY_HEADERS)
#error Use repository-owned ATL headers
#endif

using GetMitigation = BOOL (WINAPI*)(HANDLE, int, void*, SIZE_T);
using SetMitigation = BOOL (WINAPI*)(int, void*, SIZE_T);

static HMODULE kernel() noexcept { return ::GetModuleHandleW(L"kernel32.dll"); }
static GetMitigation getter() noexcept {
    return reinterpret_cast<GetMitigation>(
        ::GetProcAddress(kernel(), "GetProcessMitigationPolicy"));
}
static SetMitigation setter() noexcept {
    return reinterpret_cast<SetMitigation>(
        ::GetProcAddress(kernel(), "SetProcessMitigationPolicy"));
}
static int fail(int code, const char* stage) {
    std::fprintf(stderr, "OPENATL_MITIGATION_FAIL code=%d stage=%s error=%lu\n",
                 code, stage, static_cast<unsigned long>(::GetLastError()));
    return code;
}
static LRESULT CALLBACK thunk_target(HWND owner, UINT msg, WPARAM w, LPARAM l) {
    if (msg != WM_APP + 81 || owner == nullptr)
        return -1;
    ++*reinterpret_cast<int*>(owner);
    return static_cast<LRESULT>(w + l);
}

static int deny_dynamic_code() {
    auto set = setter();
    auto get = getter();
    if (!set || !get)
        return fail(10, "missing_mitigation_api");
    DWORD deny = 1;  // PROCESS_MITIGATION_DYNAMIC_CODE_POLICY.ProhibitDynamicCode
    if (!set(2 /*ProcessDynamicCodePolicy*/, &deny, sizeof(deny)))
        return fail(11, "enforce_dynamic_code_denial");
    DWORD readBack = 0;
    if (!get(::GetCurrentProcess(), 2, &readBack, sizeof(readBack)) ||
        (readBack & 1u) == 0)
        return fail(12, "dynamic_code_denial_not_active");
    int count = 0;
    ATL::CWndProcThunk thunk;
    if (thunk.Init(thunk_target, &count) ||
        thunk.GetCodeAddress() != nullptr ||
        thunk.GetWNDPROC() != nullptr || count != 0)
        return fail(13, "prohibited_thunk_was_accepted");
    return 0;
}

static int deny_in_child() {
    wchar_t path[MAX_PATH] = {};
    DWORD count = ::GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (count == 0 || count >= MAX_PATH)
        return fail(20, "executable_path");
    std::wstring cmd = L"\"";
    cmd += path;
    cmd += L"\" --deny-dynamic-code";
    STARTUPINFOW start{};
    start.cb = sizeof(start);
    PROCESS_INFORMATION child{};
    if (!::CreateProcessW(path, &cmd[0], nullptr, nullptr, FALSE,
                          0, nullptr, nullptr, &start, &child))
        return fail(21, "start_child");
    (void)::CloseHandle(child.hThread);
    DWORD waited = ::WaitForSingleObject(child.hProcess, 10000);
    if (waited == WAIT_TIMEOUT)
        (void)::TerminateProcess(child.hProcess, 22);
    DWORD code = 0xFFFFFFFF;
    const bool ok = waited == WAIT_OBJECT_0 &&
                     ::GetExitCodeProcess(child.hProcess, &code) && code == 0;
    (void)::CloseHandle(child.hProcess);
    return ok ? 0 : fail(23, "deny_child_failed");
}

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--deny-dynamic-code") == 0)
        return deny_dynamic_code();
    if (argc != 1)
        return fail(1, "arguments");
    auto get = getter();
    if (!get)
        return fail(2, "missing_mitigation_query");
    DWORD cfg = 0;
    if (!get(::GetCurrentProcess(), 7 /*ProcessControlFlowGuardPolicy*/,
             &cfg, sizeof(cfg)) || !(cfg & 1u))
        return fail(3, "process_CFG_not_enabled");
    DWORD dynamic = 0;
    if (!get(::GetCurrentProcess(), 2 /*ProcessDynamicCodePolicy*/,
             &dynamic, sizeof(dynamic)) || (dynamic & 1u))
        return fail(4, "unexpected_dynamic_code_denial");

    int count = 0;
    ATL::CWndProcThunk thunk;
    if (!thunk.Init(thunk_target, &count))
        return fail(5, "guarded_thunk_create");
    MEMORY_BASIC_INFORMATION info{};
    if (::VirtualQuery(thunk.GetCodeAddress(), &info, sizeof(info)) != sizeof(info) ||
        info.State != MEM_COMMIT || (info.Protect & 0xffu) != PAGE_EXECUTE_READ)
        return fail(6, "thunk_not_RX");
    WNDPROC call = thunk.GetWNDPROC();
    if (!call || call(nullptr, WM_APP + 81, 17, 25) != 42 || count != 1)
        return fail(7, "CFG_guarded_indirect_call");

    // CET hardware availability is not guaranteed by a CETCOMPAT PE flag.
    DWORD shadow = 0;
    BOOL queried = get(::GetCurrentProcess(), 15 /*ProcessUserShadowStackPolicy*/,
                       &shadow, sizeof(shadow));
    std::printf("OPENATL_MITIGATION_POLICY cfg=1 cet_query=%d shadow_active=%d\n",
                queried ? 1 : 0, queried && (shadow & 1u) ? 1 : 0);
    int result = deny_in_child();
    if (result)
        return result;
    std::puts("OPENATL_MITIGATION_OK cfg=1 rx=1 indirect=1 deny=1");
    return 0;
}
