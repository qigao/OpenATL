/* Native Windows GUI runtime acceptance for official WTL10 + pinned OpenATL.
 * This is intentionally NOT the full interactive upstream wtltest sample.
 * It does create, message, and destroy real (hidden) Win32 window/dialog
 * objects and executes the architecture-specific WndProc thunk.
 */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WINVER
#define WINVER 0x0601
#endif
#ifndef _WIN32_IE
#define _WIN32_IE 0x0601
#endif
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <atlbase.h>
#include <atlwin.h>
#include <atlapp.h>
#include <atlctrls.h>
#include "gui_runtime_resource.h"

static_assert(sizeof(void*) == 8, "WTL10 GUI native runtime expects x64/ARM64");
static_assert(_WTL_VER >= 0x1000, "Official WTL10 headers required");

WTL::CAppModule _Module;

static bool executable_read_only(const void* ptr)
{
    if (ptr == NULL) return false;
    MEMORY_BASIC_INFORMATION page = {};
    if (::VirtualQuery(ptr, &page, sizeof(page)) != sizeof(page))
        return false;
    return page.State == MEM_COMMIT &&
        (page.Protect & 0xFFu) == PAGE_EXECUTE_READ &&
        (page.Protect & PAGE_GUARD) == 0;
}

struct ThunkState {
    int invocations;
    ThunkState() : invocations(0) {}
};

static ThunkState* expected_state = NULL;
static LRESULT CALLBACK thunk_dispatch(HWND first, UINT message, WPARAM w, LPARAM l)
{
    if (message != WM_APP + 71 || (void*)first != expected_state)
        return -100;
    ++expected_state->invocations;
    return (LRESULT)(w + l);
}

static bool check_thunk()
{
    ThunkState state;
    expected_state = &state;
    ATL::CWndProcThunk thunk;
    if (!thunk.Init(NULL, NULL) ||
        !executable_read_only(thunk.GetCodeAddress()))
        return false;
    if (!thunk.Init(thunk_dispatch, &state) ||
        !executable_read_only(thunk.GetCodeAddress()))
        return false;
    WNDPROC dispatch = thunk.GetWNDPROC();
    if (dispatch == NULL) return false;
    for (int i = 0; i < 10; ++i) {
        if (dispatch(NULL, WM_APP + 71, (WPARAM)17, (LPARAM)25) != 42)
            return false;
        if (!executable_read_only(thunk.GetCodeAddress()))
            return false;
    }
    return state.invocations == 10;
}

class ProbeWindow : public ATL::CWindowImpl<ProbeWindow> {
public:
    int messages = 0;
    int destroyed = 0;
    int finalized = 0;
    int reentrant = 0;
    int nested_contract_broken = 0;

    DECLARE_WND_CLASS(L"WTL10OpenATLNativeRuntimeProbe")

    BEGIN_MSG_MAP(ProbeWindow)
        MESSAGE_HANDLER(WM_APP + 31, OnProbe)
        MESSAGE_HANDLER(WM_APP + 33, OnReentrantDestroy)
        MESSAGE_HANDLER(WM_NCDESTROY, OnNcDestroy)
    END_MSG_MAP()

    LRESULT OnProbe(UINT, WPARAM w, LPARAM l, BOOL&) {
        ++messages;
        return (LRESULT)(w + l + 100);
    }

    LRESULT OnReentrantDestroy(UINT, WPARAM, LPARAM, BOOL&) {
        ++reentrant;
        if (finalized != 0 || m_hWnd == NULL || !::DestroyWindow(m_hWnd) ||
            finalized != 0)
            nested_contract_broken = 1;
        return 0;
    }

    LRESULT OnNcDestroy(UINT, WPARAM, LPARAM, BOOL& handled) {
        ++destroyed;
        handled = FALSE;
        return 0;
    }

    void OnFinalMessage(HWND) override {
        ++finalized;
    }
};

class ProbeDialog : public ATL::CDialogImpl<ProbeDialog> {
public:
    enum { IDD = IDD_POC_DIALOG };
    int initialized = 0;
    int messages = 0;
    int destroyed = 0;
    int finalized = 0;
    int reentrant = 0;
    int nested_contract_broken = 0;

    BEGIN_MSG_MAP(ProbeDialog)
        MESSAGE_HANDLER(WM_INITDIALOG, OnInit)
        MESSAGE_HANDLER(WM_APP + 32, OnProbe)
        MESSAGE_HANDLER(WM_APP + 34, OnReentrantDestroy)
        MESSAGE_HANDLER(WM_NCDESTROY, OnNcDestroy)
    END_MSG_MAP()

    LRESULT OnInit(UINT, WPARAM, LPARAM, BOOL&) {
        ++initialized;
        return TRUE;
    }

    LRESULT OnProbe(UINT, WPARAM, LPARAM, BOOL&) {
        ++messages;
        return 0;
    }

    LRESULT OnReentrantDestroy(UINT, WPARAM, LPARAM, BOOL&) {
        ++reentrant;
        if (finalized != 0 || m_hWnd == NULL || !::DestroyWindow(m_hWnd) ||
            finalized != 0)
            nested_contract_broken = 1;
        // A handled outer message must still retire nested WM_NCDESTROY.
        return 0;
    }

    LRESULT OnNcDestroy(UINT, WPARAM, LPARAM, BOOL& handled) {
        ++destroyed;
        handled = FALSE;
        return 0;
    }

    void OnFinalMessage(HWND) override {
        ++finalized;
    }
};

static bool check_window()
{
    for (int i = 0; i < 12; ++i) {
        ProbeWindow wnd;
        RECT bounds = {0, 0, 128, 56};
        HWND h = wnd.Create(NULL, bounds, L"WTL10 hidden runtime",
                            WS_POPUP, 0);
        if (h == NULL || !::IsWindow(h) || !executable_read_only(wnd.m_thunk.GetCodeAddress()))
            return false;
        if ((WNDPROC)::GetWindowLongPtrW(h, GWLP_WNDPROC) != wnd.m_thunk.GetWNDPROC())
            return false;
        if (::SendMessageW(h, WM_APP + 31, 13, 29) != 142)
            return false;
        if (wnd.messages != 1 || !::DestroyWindow(h) || ::IsWindow(h))
            return false;
        if (wnd.destroyed != 1 || wnd.finalized != 1 || wnd.m_hWnd != NULL)
            return false;
    }
    // Nested WM_NCDESTROY defers finalization until the outer dispatch unwinds.
    for (int i = 0; i < 12; ++i) {
        ProbeWindow wnd;
        RECT bounds = {0, 0, 128, 56};
        HWND h = wnd.Create(NULL, bounds, L"WTL10 nested destroy", WS_POPUP, 0);
        if (!h || !executable_read_only(wnd.m_thunk.GetCodeAddress()))
            return false;
        ::SendMessageW(h, WM_APP + 33, 0, 0);
        if (::IsWindow(h) || wnd.reentrant != 1 ||
            wnd.nested_contract_broken != 0 || wnd.destroyed != 1 ||
            wnd.finalized != 1 || wnd.m_hWnd != NULL)
            return false;
    }
    return true;
}

static bool check_dialog()
{
    HINSTANCE executable = ::GetModuleHandleW(NULL);
    wchar_t resource_string[64] = {};
    if (::FindResourceW(executable, MAKEINTRESOURCEW(IDD_POC_DIALOG), RT_DIALOG) == NULL ||
        ::LoadStringW(executable, IDS_POC_STRING, resource_string, 64) != 24 ||
        ::lstrcmpW(resource_string, L"WTL10_RESOURCE_NATIVE_OK") != 0)
        return false;

    for (int i = 0; i < 6; ++i) {
        ProbeDialog dlg;
        HWND h = dlg.Create(NULL);
        if (!h || !::IsWindow(h) || dlg.initialized != 1)
            return false;
        if (!executable_read_only(dlg.m_thunk.GetCodeAddress()))
            return false;
        if (::GetDlgItem(h, IDOK) == NULL)
            return false;
        ::SendMessageW(h, WM_APP + 32, 0, 0);
        if (dlg.messages != 1 || !dlg.DestroyWindow() || ::IsWindow(h))
            return false;
        if (dlg.destroyed != 1 || dlg.finalized != 1 || dlg.m_hWnd != NULL)
            return false;
    }
    for (int i = 0; i < 6; ++i) {
        ProbeDialog dlg;
        HWND h = dlg.Create(NULL);
        if (!h || dlg.initialized != 1 ||
            !executable_read_only(dlg.m_thunk.GetCodeAddress()))
            return false;
        ::SendMessageW(h, WM_APP + 34, 0, 0);
        if (::IsWindow(h) || dlg.reentrant != 1 ||
            dlg.nested_contract_broken != 0 || dlg.destroyed != 1 ||
            dlg.finalized != 1 || dlg.m_hWnd != NULL) {
            fprintf(stderr,
                "Nested dialog cleanup failed: nc=%d final=%d hwnd=%p bad=%d\n",
                dlg.destroyed, dlg.finalized, (void*)dlg.m_hWnd,
                dlg.nested_contract_broken);
            return false;
        }
    }
    return true;
}

int main(void)
{
    const HRESULT apartment = ::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(apartment)) {
        fprintf(stderr, "CoInitializeEx failed: 0x%08lx\n", (unsigned long)apartment);
        return 10;
    }
    HRESULT init = _Module.Init(NULL, ::GetModuleHandleW(NULL));
    if (FAILED(init)) {
        ::CoUninitialize();
        fprintf(stderr, "WTL CAppModule init failed\n");
        return 11;
    }

    // Do not keep a live GUI window when releasing executable thunk pages.
    const bool thunk_ok = check_thunk();
    const bool window_ok = thunk_ok && check_window();
    const bool dialog_ok = window_ok && check_dialog();

    _Module.Term();
    ::CoUninitialize();

    if (!thunk_ok || !window_ok || !dialog_ok) {
        fprintf(stderr, "WTL10 GUI runtime FAILURE thunk=%d window=%d dialog=%d err=%lu\n",
                (int)thunk_ok, (int)window_ok, (int)dialog_ok,
                (unsigned long)::GetLastError());
        return !thunk_ok ? 20 : (!window_ok ? 21 : 22);
    }
    puts("WTL10 GUI runtime PASS: W^X thunk x64/ARM64, true message maps, resource dialog, NCDESTROY, OnFinalMessage");
    return 0;
}
