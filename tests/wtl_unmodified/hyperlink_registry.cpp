// Original WTL10 hyperlink HKCU registry behavior, no source rewriting.
// RegOverridePredefKey is process-scoped; this is a dedicated executable mode
// so unrelated application/COM tests never observe a redirected HKCU.
// Real user IE settings are not opened for writing or changed.
#include <atlbase.h>
#include <atlwin.h>
#include <atlapp.h>
#include <atlctrls.h>
#include <atlctrlx.h>
#include <atlreg.h>
#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <string>
#include <cstdio>

namespace {
bool NoCom() noexcept {
    ULONG_PTR context = 0;
    return ::CoGetContextToken(&context) == CO_E_NOTINITIALIZED;
}
struct ScopedRegistry {
    std::wstring path;
    HKEY root = nullptr;
    bool created = false, redirected = false;
    bool Open() {
        GUID uuid{};
        if (FAILED(::CoCreateGuid(&uuid))) return false;
        wchar_t suffix[40]{};
        if (!::StringFromGUID2(uuid, suffix, 40)) return false;
        path = std::wstring(L"Software\\OpenATL.WTL.HyperLink.") + suffix;
        DWORD disposition = 0;
        if (::RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr,
                REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr, &root,
                &disposition) != ERROR_SUCCESS)
            return false;
        created = disposition == REG_CREATED_NEW_KEY;
        return created;
    }
    bool AddColors() {
        if (!root) return false;
        HKEY settings = nullptr;
        if (::RegCreateKeyExW(root,
                L"Software\\Microsoft\\Internet Explorer\\Settings", 0, nullptr,
                REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE, nullptr,
                &settings, nullptr) != ERROR_SUCCESS)
            return false;
        const wchar_t anchor[] = L"12,34,56";
        const wchar_t visited[] = L"101,102,103";
        LSTATUS first = ::RegSetValueExW(settings, L"Anchor Color", 0, REG_SZ,
                    reinterpret_cast<const BYTE*>(anchor), sizeof(anchor));
        LSTATUS second = ::RegSetValueExW(settings, L"Anchor Color Visited", 0, REG_SZ,
                    reinterpret_cast<const BYTE*>(visited), sizeof(visited));
        const LSTATUS closed = ::RegCloseKey(settings);
        return first == ERROR_SUCCESS && second == ERROR_SUCCESS &&
               closed == ERROR_SUCCESS;
    }
    bool Override() {
        if (!root || !created || redirected) return false;
        if (::RegOverridePredefKey(HKEY_CURRENT_USER, root) != ERROR_SUCCESS)
            return false;
        redirected = true;
        return true;
    }
    bool Close() {
        bool okay = true;
        if (redirected) {
            const LSTATUS status = ::RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
            okay = status == ERROR_SUCCESS && okay;
            if (status == ERROR_SUCCESS) redirected = false;
        }
        if (root) {
            const LSTATUS status = ::RegCloseKey(root);
            okay = status == ERROR_SUCCESS && okay;
            if (status == ERROR_SUCCESS) root = nullptr;
        }
        if (created && !redirected) {
            const LSTATUS status = ::RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str());
            okay = status == ERROR_SUCCESS && okay;
            if (status == ERROR_SUCCESS) created = false;
        }
        return okay;
    }
    ~ScopedRegistry() { if (!Close()) std::terminate(); }
};
struct ModuleOwner {
    WTL::CAppModule module{};
    bool ready = false;
    bool Init(HINSTANCE image) {
        ready = SUCCEEDED(module.Init(nullptr, image, nullptr));
        return ready;
    }
    ~ModuleOwner() { if (ready) module.Term(); }
};
struct ParentWindow {
    HWND hwnd = nullptr;
    ~ParentWindow() { if (::IsWindow(hwnd)) ::DestroyWindow(hwnd); }
};

bool OneHyperlinkCycle() {
    if (!NoCom()) return false;
    ScopedRegistry isolated;
    if (!isolated.Open() || !isolated.AddColors() || !isolated.Override())
        return false;
    {
        ATL::CRegKey verify;
        wchar_t got[24]{};
        ULONG count = static_cast<ULONG>(sizeof(got) / sizeof(got[0]));
        if (verify.Open(HKEY_CURRENT_USER,
              L"Software\\Microsoft\\Internet Explorer\\Settings",
              KEY_READ) != ERROR_SUCCESS ||
            verify.QueryStringValue(L"Anchor Color", got, &count) != ERROR_SUCCESS ||
            ::lstrcmpW(got, L"12,34,56") != 0 ||
            verify.Close() != ERROR_SUCCESS)
            return false;
    }
    HINSTANCE image = ::GetModuleHandleW(nullptr);
    if (!image) return false;
    ModuleOwner module;
    if (!module.Init(image)) return false;
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_WIN95_CLASSES};
    if (!::InitCommonControlsEx(&common)) return false;
    ParentWindow parent;
    parent.hwnd = ::CreateWindowExW(0, L"STATIC", L"WTL Hyperlink host",
                   WS_OVERLAPPED, 0, 0, 320, 80, nullptr, nullptr, image, nullptr);
    if (!parent.hwnd) return false;
    bool correct = false;
    {
        WTL::CHyperLink hyperlink;
        RECT bounds{0,0,260,28};
        const HWND child = hyperlink.Create(parent.hwnd, bounds, L"Link colors",
                                          WS_CHILD | WS_VISIBLE);
        correct = child && ::IsWindow(child) &&
                  hyperlink.m_clrLink == RGB(12,34,56) &&
                  hyperlink.m_clrVisited == RGB(101,102,103);
        if (child && ::IsWindow(child))
            correct = (::DestroyWindow(child) != 0) && correct;
    }
    const bool retired = ::DestroyWindow(parent.hwnd) != 0;
    parent.hwnd = nullptr;
    // The module is still live until all Win32-owned WTL controls and
    // their fonts/tooltips have been destroyed.
    if (!retired || !correct || !NoCom()) return false;
    if (!isolated.Close()) return false;
    return NoCom();
}
} // namespace
bool RunOriginalHyperlinkRegistryTests() {
    for (int i = 0; i < 3; ++i) {
        if (!OneHyperlinkCycle()) {
            std::fprintf(stderr, "WTL_HYPERLINK_FAIL cycle=%d\n", i);
            return false;
        }
    }
    std::puts("OPENATL_WTL_HYPERLINK_OK registry=1 original=1 colors=1 no_com=1 cleanup=1");
    return true;
}
