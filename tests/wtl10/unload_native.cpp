// Real DLL reload + active thunk callback lifetime acceptance.
// Unload is refused until the worker's final HWND/class/callback is retired.
#include <windows.h>
#include <objbase.h>
#include <cstdio>
#include <cwchar>

using Start = BOOL (WINAPI*)(HMODULE);
using ProbeWindowFn = HWND (WINAPI*)();
using Address = void* (WINAPI*)();
using WaitEntered = BOOL (WINAPI*)(DWORD);
using Release = BOOL (WINAPI*)();
using CanUnload = BOOL (WINAPI*)();
using Stop = DWORD (WINAPI*)();

constexpr UINT kBlockedMessage = WM_APP + 90;
constexpr int kCycles = 8;
static int fail(int code, const char* stage, int cycle) {
    std::fprintf(stderr,
                 "OPENATL_UNLOAD_FAIL code=%d stage=%s cycle=%d winerr=%lu\n",
                 code, stage, cycle, static_cast<unsigned long>(::GetLastError()));
    return code;
}
struct Sender {
    HWND hwnd = nullptr;
    DWORD result = 0xFFFFFFFF;
    DWORD transferred = 0;
};
static DWORD WINAPI send_blocked(void* opaque) {
    auto* request = static_cast<Sender*>(opaque);
    DWORD_PTR output = 0;
    BOOL ok = ::SendMessageTimeoutW(request->hwnd, kBlockedMessage, 17, 25,
                                     SMTO_ABORTIFHUNG | SMTO_BLOCK, 8000, &output);
    request->transferred = ok != 0;
    request->result = static_cast<DWORD>(output);
    return ok && output == 42 ? 0 : 1;
}

int main() {
    ULONG_PTR context = 0;
    if (::CoGetContextToken(&context) != CO_E_NOTINITIALIZED)
        return fail(1, "unexpected_com_apartment", 0);
    wchar_t dllPath[MAX_PATH] = {};
    const DWORD chars = ::GetModuleFileNameW(nullptr, dllPath, MAX_PATH);
    if (chars == 0 || chars >= MAX_PATH)
        return fail(2, "self_executable_location", 0);
    wchar_t* separator = std::wcsrchr(dllPath, L'\\');
    if (!separator)
        return fail(3, "binary_parent_directory", 0);
    ++separator;
    const wchar_t name[] = L"openatl_unload_probe.dll";
    if (separator - dllPath + sizeof(name) / sizeof(*name) > MAX_PATH)
        return fail(4, "dll_path_too_long", 0);
    std::wcscpy(separator, name);
    for (int cycle = 0; cycle < kCycles; ++cycle) {
        HMODULE dll = ::LoadLibraryW(dllPath);
        if (dll == nullptr)
            return fail(5, "LoadLibrary", cycle);
        auto start = reinterpret_cast<Start>(::GetProcAddress(dll, "OpenATLStart"));
        auto window = reinterpret_cast<ProbeWindowFn>(::GetProcAddress(dll, "OpenATLWindow"));
        auto address = reinterpret_cast<Address>(::GetProcAddress(dll, "OpenATLThunkAddress"));
        auto wait = reinterpret_cast<WaitEntered>(::GetProcAddress(dll, "OpenATLWaitEntered"));
        auto release = reinterpret_cast<Release>(::GetProcAddress(dll, "OpenATLReleaseCallback"));
        auto canUnload = reinterpret_cast<CanUnload>(::GetProcAddress(dll, "OpenATLCanUnload"));
        auto stop = reinterpret_cast<Stop>(::GetProcAddress(dll, "OpenATLStop"));
        if (!start || !window || !address || !wait || !release ||
            !canUnload || !stop || !start(dll))
            return fail(6, "active_dll_start", cycle);
        const HWND owner = window();
        void* thunk = address();
        MEMORY_BASIC_INFORMATION mbi{};
        if (owner == nullptr || !::IsWindow(owner) ||
            thunk == nullptr || canUnload() ||
            ::VirtualQuery(thunk, &mbi, sizeof(mbi)) != sizeof(mbi) ||
            mbi.State != MEM_COMMIT || (mbi.Protect & 0xFFu) != PAGE_EXECUTE_READ)
            return fail(7, "live_callback_owner_not_RX", cycle);
        Sender sender{};
        sender.hwnd = owner;
        HANDLE caller = ::CreateThread(nullptr, 0, send_blocked,
                                       &sender, 0, nullptr);
        if (caller == nullptr)
            return fail(8, "create_sender", cycle);
        if (!wait(5000) || canUnload() || stop() != ERROR_BUSY)
            return fail(9, "busy_callback_not_protected", cycle);
        if (!release())
            return fail(10, "release_callback", cycle);
        if (::WaitForSingleObject(caller, 5000) != WAIT_OBJECT_0)
            return fail(11, "blocked_sender_not_retired", cycle);
        DWORD senderExit = 0xFFFFFFFF;
        if (!::GetExitCodeThread(caller, &senderExit) ||
            senderExit != 0 || sender.transferred == 0 ||
            sender.result != 42)
            return fail(12, "callback_return", cycle);
        ::CloseHandle(caller);
        if (stop() != ERROR_SUCCESS || !canUnload() ||
            ::IsWindow(owner))
            return fail(13, "bounded_window_thread_shutdown", cycle);
        if (!::FreeLibrary(dll))
            return fail(14, "FreeLibrary", cycle);
        if (::GetModuleHandleW(L"openatl_unload_probe.dll") != nullptr)
            return fail(15, "dll_still_loaded", cycle);
        MEMORY_BASIC_INFORMATION after{};
        if (::VirtualQuery(thunk, &after, sizeof(after)) != sizeof(after) ||
            after.State != MEM_FREE)
            return fail(16, "stale_thunk_page_after_DLL_unload", cycle);
    }
    if (::CoGetContextToken(&context) != CO_E_NOTINITIALIZED)
        return fail(17, "implicit_com_apartment", kCycles);
    std::puts("OPENATL_UNLOAD_OK cycles=8 blocked=1 busy=1 retire=1 free=1 no_com=1");
    return 0;
}
