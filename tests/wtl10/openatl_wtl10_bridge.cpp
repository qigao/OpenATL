/* WTL 10.0.10320 + OpenATL pinned C++17 ABI POC.
 * Clang owns all C++/COM/WTL objects; TinyCC is strictly a C caller.
 * Uses raw hidden Win32 windows and WTL10 control wrappers.
 * Does not instantiate OpenATL executable wndproc thunks (RWX not qualified).
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

#include <atlbase.h>
#include <atlwin.h>
#include <atlapp.h>
#include <atlctrls.h>
#include <atlframe.h>
#include <atldlgs.h>
#include <commctrl.h>
#include <stdint.h>

static_assert(_WTL_VER >= 0x1000, "Required actual WTL 10, not legacy WTL8");
static_assert(sizeof(void *) == 8, "This provider tests Windows x64/ARM64");

WTL::CAppModule _Module;
class IdleProbe final : public WTL::CIdleHandler {
public:
    BOOL OnIdle() override { return FALSE; }
};

extern "C" __declspec(dllexport) unsigned __cdecl
poc_atl_abi_version(void) { return 1u; }

extern "C" __declspec(dllexport) int __cdecl
poc_atl_probe(const int *values, unsigned count) {
    if (!values || count > 16) return -1;
    int total = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (values[i] < 0 || values[i] > 10000 || total > 100000 - values[i])
            return -2;
        total += values[i];
    }
    HRESULT co = ::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(co)) return -3;
    HRESULT hr = _Module.Init(NULL, ::GetModuleHandleW(NULL));
    if (FAILED(hr)) { ::CoUninitialize(); return -4; }
    bool ok = true;
    {
        WTL::CMessageLoop loop;
        IdleProbe idle;
        bool registered = _Module.AddMessageLoop(&loop) != FALSE;
        if (!registered) ok = false;
        if (registered && _Module.GetMessageLoop() != &loop) ok = false;
        if (registered) {
            if (loop.AddIdleHandler(&idle)) {
                if (!loop.RemoveIdleHandler(&idle)) ok = false;
            } else ok = false;
        }

        INITCOMMONCONTROLSEX icc = {sizeof(icc),
            ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES};
        if (!::InitCommonControlsEx(&icc)) ok = false;
        HWND parent = ::CreateWindowExW(0, L"STATIC", L"WTL 10 test",
            WS_POPUP | WS_CLIPCHILDREN, 0, 0, 160, 125, NULL, NULL,
            ::GetModuleHandleW(NULL), NULL);
        if (!parent) ok = false;
        if (parent && ok) {
            HWND hEdit = ::CreateWindowExW(0, L"EDIT", L"seed",
                WS_CHILD | ES_AUTOHSCROLL, 0, 0, 160, 24, parent,
                (HMENU)(UINT_PTR)101, ::GetModuleHandleW(NULL), NULL);
            HWND hButton = ::CreateWindowExW(0, L"BUTTON", L"tick",
                WS_CHILD | BS_AUTOCHECKBOX, 0, 24, 160, 24, parent,
                (HMENU)(UINT_PTR)102, ::GetModuleHandleW(NULL), NULL);
            HWND hList = ::CreateWindowExW(0, WC_LISTVIEWW, L"",
                WS_CHILD | LVS_REPORT, 0, 49, 160, 34, parent,
                (HMENU)(UINT_PTR)103, ::GetModuleHandleW(NULL), NULL);
            HWND hTree = ::CreateWindowExW(0, WC_TREEVIEWW, L"",
                WS_CHILD | TVS_HASLINES, 0, 84, 160, 38, parent,
                (HMENU)(UINT_PTR)104, ::GetModuleHandleW(NULL), NULL);
            if (!hEdit || !hButton || !hList || !hTree) ok = false;
            if (ok) {
                WTL::CEdit edit(hEdit);
                WTL::CButton button(hButton);
                WTL::CListViewCtrl list(hList);
                WTL::CTreeViewCtrl tree(hTree);
                wchar_t received[64] = {};
                if (!edit.SetWindowText(L"WTL10 accepted") ||
                    edit.GetWindowText(received, 64) != 14 ||
                    ::lstrcmpW(received, L"WTL10 accepted")) ok = false;
                button.SetCheck(BST_CHECKED);
                if (button.GetCheck() != BST_CHECKED) ok = false;
                if (list.InsertItem(0, L"item") != 0 ||
                    list.GetItemCount() != 1) ok = false;
                const DWORD_PTR large = ((DWORD_PTR)0xFEDCBA98ULL << 32) |
                                         (DWORD_PTR)0x76543210U;
                if (!list.SetItemData(0, large) ||
                    list.GetItemData(0) != large) ok = false;
                HTREEITEM root = tree.InsertItem(L"root", TVI_ROOT, TVI_LAST);
                if (!root || !tree.SetItemData(root, large) ||
                    tree.GetItemData(root) != large) ok = false;
            }
        }
        if (parent) {
            if (!::DestroyWindow(parent) || ::IsWindow(parent)) ok = false;
        }
        if (registered && !_Module.RemoveMessageLoop()) ok = false;
    }
    _Module.Term();
    ::CoUninitialize();
    return ok ? total : -5;
}
