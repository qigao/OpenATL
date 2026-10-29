// Actual WTL10 CServerAppModule::MonitorProc lifetime, with an explicitly
// retained Win32 worker handle. No modification of installed WTL headers.
// The legacy StartMonitor() itself closes its worker handle and does not
// establish a join contract; it is deliberately NOT treated as qualified.
// Tests never concurrently write original WTL's non-atomic m_bActivity.
#include <atlbase.h>
#include <atlwin.h>
#include <atlapp.h>
#include <windows.h>
#include <cstdio>
#include <exception>

namespace {
bool NoComApartment() noexcept {
    ULONG_PTR token = 0;
    return ::CoGetContextToken(&token) == CO_E_NOTINITIALIZED;
}

enum class MonitorCase { idle, pinned, preStartUnlock };

bool RunMonitorCase(MonitorCase scenario) {
    const bool pinned = scenario == MonitorCase::pinned;
    const bool preStartUnlock = scenario == MonitorCase::preStartUnlock;
    if (!NoComApartment()) return false;
    const HINSTANCE image = ::GetModuleHandleW(nullptr);
    if (!image) return false;
    WTL::CServerAppModule module{};
    // Original WTL's CServerAppModule does not initialize these members in
    // its Init(): the native owning client must establish initial state.
    module.m_hEventShutdown = nullptr;
    module.m_bActivity = false;
    if (FAILED(module.Init(nullptr, image, nullptr))) return false;
    module.m_dwTimeOut = 20;
    module.m_dwPause = 1;

    // PostThreadMessage(WM_QUIT) requires the destination thread to have
    // created a message queue BEFORE the worker reaches MonitorShutdown().
    MSG queued{};
    ::PeekMessageW(&queued, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

    HANDLE event = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event) { module.Term(); return false; }
    module.m_hEventShutdown = event;
    if (pinned && module.Lock() != 1) std::terminate();
    bool preStartSignalOkay = true;
    if (preStartUnlock) {
        // This directly tests the unchanged WTL CServerAppModule::Unlock
        // side effects, but no worker exists yet: no m_bActivity data race.
        if (module.Lock() != 1) std::terminate();
        const LONG count = module.Unlock();
        preStartSignalOkay =
            count == 0 && module.m_bActivity &&
            ::WaitForSingleObject(event, 0) == WAIT_OBJECT_0;
    }

    DWORD workerId = 0;
    // Retain a genuine join handle. Starting suspended makes the initial
    // event signal deterministic, even on heavily loaded ARM64 runners.
    HANDLE worker = ::CreateThread(nullptr, 0,
        &WTL::CServerAppModule::MonitorProc, &module,
        CREATE_SUSPENDED, &workerId);
    if (!worker) {
        if (pinned) module.CComModule::Unlock();
        ::CloseHandle(event);
        module.m_hEventShutdown = nullptr;
        module.Term();
        return false;
    }
    if (!::SetEvent(event) || ::ResumeThread(worker) != 1)
        std::terminate(); // Never retire a module with a live/suspended worker.

    bool good = workerId != 0 && preStartSignalOkay;
    if (pinned) {
        // At least several idle timeouts pass with the module pinned.
        const DWORD blocked = ::WaitForSingleObject(worker, 130);
        good = good && blocked == WAIT_TIMEOUT;
        const LONG remaining = module.CComModule::Unlock();
        good = good && remaining == 0;
        if (blocked == WAIT_TIMEOUT && !::SetEvent(event))
            std::terminate();
    }
    const DWORD joined = ::WaitForSingleObject(worker, 3000);
    if (joined != WAIT_OBJECT_0)
        std::terminate(); // Cannot safely Term() a still-running WTL worker.
    good = good && ::CloseHandle(worker) != 0;
    good = good && module.m_hEventShutdown == nullptr;
    good = good && module.GetLockCount() == 0;

    MSG quit{};
    const BOOL posted = ::PeekMessageW(&quit, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE);
    good = good && posted != 0 && quit.message == WM_QUIT;
    module.Term();
    return good && NoComApartment();
}

// This is a separate *process-only* admission of the unchanged upstream
// CServerAppModule::StartMonitor(). StartMonitor() discards its worker handle,
// so there is no safe join/Term proof. Never destroy the module while the
// worker might still reference it: ExitProcess retires the isolated fixture.
// No concurrent Unlock(), no data race on upstream's plain bool m_bActivity.
[[noreturn]] void MonitorProcessExit(UINT status, const char* error = nullptr) {
    if (error)
        std::fprintf(stderr, "WTL_START_MONITOR_PROCESS_FAIL reason=%s\n", error);
    else
        std::puts("OPENATL_WTL_START_MONITOR_PROCESS_OK quit=1 original=1 join=UNQUALIFIED");
    std::fflush(stdout);
    std::fflush(stderr);
    ::ExitProcess(status);
}

void StartOriginalMonitorProcessOnly() {
    if (!NoComApartment()) MonitorProcessExit(21, "unexpected-com-apartment");
    const HINSTANCE image = ::GetModuleHandleW(nullptr);
    if (!image) MonitorProcessExit(22, "image");
    WTL::CServerAppModule module{};
    module.m_hEventShutdown = nullptr;
    module.m_bActivity = false;
    if (FAILED(module.Init(nullptr, image, nullptr)))
        MonitorProcessExit(23, "module-init");
    module.m_dwTimeOut = 20;
    module.m_dwPause = 1;

    // PostThreadMessage(WM_QUIT) requires a queue on this owning thread.
    MSG queue{};
    ::PeekMessageW(&queue, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    if (!module.StartMonitor()) MonitorProcessExit(24, "original-start");
    HANDLE wake = module.m_hEventShutdown;
    if (!wake || !::SetEvent(wake)) MonitorProcessExit(25, "wake");

    // The upstream worker reads a stable module and owns the only writes to
    // m_bActivity. WM_QUIT demonstrates the idle startup path, not a join.
    const DWORD started = ::GetTickCount();
    for (;;) {
        MSG message{};
        if (::PeekMessageW(&message, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {
            if (message.message != WM_QUIT || !NoComApartment())
                MonitorProcessExit(26, "quit-or-com");
            MonitorProcessExit(0);
        }
        if (::GetTickCount() - started > 5000)
            MonitorProcessExit(27, "worker-timeout");
        if (::MsgWaitForMultipleObjectsEx(0, nullptr, 100, QS_POSTMESSAGE,
                                          MWMO_INPUTAVAILABLE) == WAIT_FAILED)
            MonitorProcessExit(28, "message-wait");
    }
}
} // namespace

void RunStartOriginalMonitorProcessOnly() {
    StartOriginalMonitorProcessOnly();
}

bool RunServerMonitorTests() {
    for (int i = 0; i < 5; ++i) {
        if (!RunMonitorCase(MonitorCase::idle) ||
            !RunMonitorCase(MonitorCase::pinned) ||
            !RunMonitorCase(MonitorCase::preStartUnlock)) {
            std::fprintf(stderr, "WTL_SERVER_MONITOR_FAIL cycle=%d\n", i);
            return false;
        }
    }
    std::puts("OPENATL_WTL_SERVER_MONITOR_OK cycles=15 joined=1 pinned=1 signal=1 quit=1 no_com=1");
    return true;
}
