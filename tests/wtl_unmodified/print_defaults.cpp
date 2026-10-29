// Original WTL CPrintDialogEx::GetDefaults against the actual Win32
// PrintDlgEx/Winspool APIs. No fake printer, dialog replacement or WTL edit.
// A missing host default printer is explicit UNQUALIFIED evidence, not a
// successful printer UI test. The separate real modal-UI gate stays open.
#include <atlbase.h>
#include <atlcom.h>
#include <atlwin.h>
#include <atlapp.h>
#include <atlframe.h>
#include <atlctrls.h>
#include <atldlgs.h>
#include <winspool.h>
#include <cstddef>
#include <cstdio>
#include <vector>

namespace {
bool NoComApartment() {
    ULONG_PTR token = 0;
    return ::CoGetContextToken(&token) == CO_E_NOTINITIALIZED;
}

struct PrinterResources {
    PRINTDLGEX& values;
    explicit PrinterResources(PRINTDLGEX& v) : values(v) {}
    PrinterResources(const PrinterResources&) = delete;
    PrinterResources& operator=(const PrinterResources&) = delete;
    ~PrinterResources() { (void)Release(); }
    bool Release() {
        bool okay = true;
        // hDC is only a defined output if PD_RETURNDC/PD_RETURNIC
        // requests it. This metadata-only probe must not interpret an
        // unspecified hDC value as an owned printer DC.
        if ((values.Flags & PD_RETURNDC) && values.hDC) {
            if (::DeleteDC(values.hDC)) values.hDC = nullptr;
            else okay = false;
        }
        if (values.hDevMode) {
            HGLOBAL h = values.hDevMode;
            if (::GlobalFree(h) == nullptr) values.hDevMode = nullptr;
            else okay = false;
        }
        if (values.hDevNames) {
            HGLOBAL h = values.hDevNames;
            if (::GlobalFree(h) == nullptr) values.hDevNames = nullptr;
            else okay = false;
        }
        return okay;
    }
};

bool HasDefaultPrinter(bool& present) {
    DWORD characters = 0;
    ::SetLastError(ERROR_SUCCESS);
    if (::GetDefaultPrinterW(nullptr, &characters)) return false;
    DWORD failure = ::GetLastError();
    if (failure == ERROR_FILE_NOT_FOUND) {
        present = false;
        return true;
    }
    if (failure != ERROR_INSUFFICIENT_BUFFER ||
        characters < 2 || characters > 32768) return false;
    std::vector<wchar_t> name(characters);
    if (!::GetDefaultPrinterW(name.data(), &characters) ||
        characters < 2 || characters > name.size() ||
        name.front() == L'\0' || name[characters - 1] != L'\0')
        return false;
    present = true;
    return true;
}

bool ValidateReturnedPrinter(const PRINTDLGEX& values) {
    if (!values.hDevNames || !values.hDevMode)
        return false; // Only Win32 default printer metadata is required here.

    const SIZE_T nameBytes = ::GlobalSize(values.hDevNames);
    const DEVNAMES* info = static_cast<const DEVNAMES*>(::GlobalLock(values.hDevNames));
    if (!info) return false;
    bool named = false;
    if (nameBytes >= sizeof(DEVNAMES) && nameBytes % sizeof(wchar_t) == 0) {
        const size_t chars = nameBytes / sizeof(wchar_t);
        const size_t offset = info->wDeviceOffset;
        if (offset < chars) {
            const auto* data = reinterpret_cast<const wchar_t*>(info);
            if (data[offset]) {
                for (size_t i = offset + 1; i < chars; ++i) {
                    if (!data[i]) { named = true; break; }
                }
            }
        }
    }
    ::GlobalUnlock(values.hDevNames);
    if (!named) return false;

    const SIZE_T modeBytes = ::GlobalSize(values.hDevMode);
    const DEVMODEW* mode = static_cast<const DEVMODEW*>(::GlobalLock(values.hDevMode));
    if (!mode) return false;
    constexpr size_t required = offsetof(DEVMODEW, dmFields) + sizeof(DWORD);
    bool modeOkay = modeBytes >= required &&
        mode->dmSize >= required && mode->dmSize <= modeBytes;
    ::GlobalUnlock(values.hDevMode);
    return modeOkay;
}
}

bool RunOriginalPrintDefaultsProbe() {
    if (!NoComApartment()) return false;
    bool hasDefault = false;
    if (!HasDefaultPrinter(hasDefault)) {
        std::fprintf(stderr, "WTL_PRINT_DEFAULTS_FAIL stage=winspool_default\n");
        return false;
    }

    const HRESULT apartment = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(apartment)) {
        std::fprintf(stderr, "WTL_PRINT_DEFAULTS_FAIL stage=sta_apartment hr=0x%08lx\n",
                     static_cast<unsigned long>(apartment));
        return false;
    }
    bool good = true;
    HRESULT wtlStatus = E_UNEXPECTED;
    HRESULT win32Status = E_UNEXPECTED;
    bool wtlMetadata = false;
    bool win32Metadata = false;

    // PRINTDLGEXW.hwndOwner MUST be a valid HWND even with PD_RETURNDEFAULT.
    // A console test has no active HWND; WTL's fallback GetActiveWindow()
    // can return NULL, which Windows rejects with ERROR_INVALID_HANDLE.
    HWND owner = ::CreateWindowExW(0, L"STATIC", L"OpenATL printer API owner",
                                  WS_OVERLAPPED, 0, 0, 1, 1, nullptr, nullptr,
                                  ::GetModuleHandleW(nullptr), nullptr);
    if (!owner || !::IsWindow(owner)) good = false;
    if (good) {
        // Original WTL defaults include PD_NOPAGENUMS. Without that flag,
        // Win32 requires non-null lpPageRanges and nMaxPageRanges > 0.
        constexpr DWORD flags = PD_ALLPAGES | PD_USEDEVMODECOPIES |
            PD_NOPAGENUMS | PD_NOSELECTION | PD_NOCURRENTPAGE | PD_NOWARNING;
        {
            WTL::CPrintDialogEx dialog(flags, owner);
            PrinterResources memory(dialog.m_pdex);
            if (dialog.m_pdex.hwndOwner != owner ||
                dialog.m_pdex.Flags != flags ||
                dialog.m_pdex.hDevNames || dialog.m_pdex.hDevMode ||
                dialog.m_pdex.hDC || dialog.m_pdex.lpCallback)
                good = false;
            if (good) {
                wtlStatus = dialog.GetDefaults(); // unchanged original WTL
                // Upstream WTL removes PD_RETURNDEFAULT after calling Win32.
                if ((dialog.m_pdex.Flags & PD_RETURNDEFAULT) ||
                    dialog.m_pdex.hwndOwner != owner ||
                    dialog.m_pdex.lpCallback || dialog.m_hWnd)
                    good = false;
                if (SUCCEEDED(wtlStatus))
                    wtlMetadata = ValidateReturnedPrinter(dialog.m_pdex);
            }
            if (!memory.Release()) good = false;
        }
        // Independent documented Windows API reference; same valid input
        // flags, HWND and COM apartment. Not a fallback renderer/provider.
        {
            PRINTDLGEXW system{};
            system.lStructSize = sizeof(system);
            system.hwndOwner = owner;
            system.Flags = flags | PD_RETURNDEFAULT;
            system.nStartPage = START_PAGE_GENERAL;
            PrinterResources memory(system);
            win32Status = ::PrintDlgExW(&system);
            if (SUCCEEDED(win32Status))
                win32Metadata = ValidateReturnedPrinter(system);
            if (!memory.Release()) good = false;
        }
    }

    if (owner) {
        if (!::DestroyWindow(owner) || ::IsWindow(owner)) good = false;
    }
    ::CoUninitialize();
    if (wtlStatus != win32Status) good = false;
    if (SUCCEEDED(wtlStatus)) {
        if (!hasDefault || !wtlMetadata || !win32Metadata) good = false;
    } else if (wtlMetadata || win32Metadata) {
        good = false;
    }
    if (!NoComApartment()) good = false;

    if (!good) {
        std::fprintf(stderr,
                     "WTL_PRINT_DEFAULTS_FAIL stage=win32_parity default=%d "
                     "wtl=0x%08lx raw=0x%08lx\n",
                     hasDefault ? 1 : 0, static_cast<unsigned long>(wtlStatus),
                     static_cast<unsigned long>(win32Status));
        return false;
    }
    const bool metadataQualified = hasDefault && SUCCEEDED(wtlStatus);
    std::printf("WTL_PRINTER_READINESS default=%d win32=0x%08lx wtl=0x%08lx "
                "metadata=%s modal=UNQUALIFIED\n",
                hasDefault ? 1 : 0, static_cast<unsigned long>(win32Status),
                static_cast<unsigned long>(wtlStatus),
                metadataQualified ? "QUALIFIED" : "UNQUALIFIED");
    // A matching failed OS+WTL call proves parity only; device/GUI unqualified.
    std::puts("OPENATL_WTL_PRINT_DEFAULTS_PARITY_OK win32=1 cleanup=1 modal=UNQUALIFIED");
    return true;
}
