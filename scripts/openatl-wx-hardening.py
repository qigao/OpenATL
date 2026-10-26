#!/usr/bin/env python3
"""WTL10 POC-only W^X/ARM64 thunk qualification patch on exact-pinned OpenATL.

Fail closed on any upstream mismatch. The upstream source remains untouched.
This is NOT a generic or production-compliant Windows code-generation backend.
"""
from pathlib import Path
import subprocess
import sys

PIN = "7b587f3e10f3f8d93a01522033de80007368bad5"

REPLACEMENTS = [
(
"""        // Allocate executable memory
        if (pThunkCode == NULL) {
            pThunkCode = (ThunkCode*)::VirtualAlloc(NULL, sizeof(ThunkCode),
                MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            if (pThunkCode == NULL)
                return FALSE;
        }

        // Allow writing to executable memory
        DWORD dwOldProtect = 0;
        ::VirtualProtect(pThunkCode, sizeof(ThunkCode), PAGE_EXECUTE_READWRITE, &dwOldProtect);""",
"""        // Fail closed: never leave executable thunk pages writable.
        // Caller guarantees no concurrent execution while reinitializing.
        if (pThunkCode == NULL) {
            pThunkCode = (ThunkCode*)ThunkAlloc(NULL, sizeof(ThunkCode),
                MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (pThunkCode == NULL)
                return FALSE;
        } else {
            DWORD oldProtection = 0;
            if (!ThunkProtect(pThunkCode, sizeof(ThunkCode), PAGE_READWRITE,
                                  &oldProtection)) {
                // A failed re-init must never retain a callable old thunk.
                ThunkFree(pThunkCode);
                pThunkCode = NULL;
                return FALSE;
            }
        }"""
),
(
"""        pThunkCode->ldr_x16 = 0x58000070;  // ldr x16, [pc, #12]""",
"""        pThunkCode->ldr_x16 = 0x58000090;  // ldr x16, [pc, #16], pProc at byte 20"""
),
(
"""        // Restore original protection and flush instruction cache
        ::VirtualProtect(pThunkCode, sizeof(ThunkCode), dwOldProtect, &dwOldProtect);
        ::FlushInstructionCache(::GetCurrentProcess(), pThunkCode, sizeof(ThunkCode));
        return TRUE;""",
"""        // Commit RX only after all stores. A protection/flush failure must
        // never expose a writable-and-executable thunk to a window.
        DWORD oldProtection = 0;
        if (!ThunkProtect(pThunkCode, sizeof(ThunkCode), PAGE_EXECUTE_READ,
                              &oldProtection) ||
            !ThunkFlush(::GetCurrentProcess(), pThunkCode,
                                     sizeof(ThunkCode))) {
            ThunkFree(pThunkCode);
            pThunkCode = NULL;
            return FALSE;
        }
        return TRUE;"""
),
(
"""        pThis->m_thunk.Init(pThis->GetWindowProc(), pThis);
        WNDPROC pProc = pThis->m_thunk.GetWndProc();""",
"""        if (!pThis->m_thunk.Init(pThis->GetWindowProc(), pThis)) {
            ::SetLastError(ERROR_OUTOFMEMORY);
            return FALSE;
        }
        WNDPROC pProc = pThis->m_thunk.GetWndProc();"""
),
(
"""        pThis->m_thunk.Init((WNDPROC)pThis->GetDialogProc(), pThis);
        DLGPROC pProc = (DLGPROC)pThis->m_thunk.GetWndProc();""",
"""        if (!pThis->m_thunk.Init((WNDPROC)pThis->GetDialogProc(), pThis)) {
            ::SetLastError(ERROR_OUTOFMEMORY);
            return FALSE;
        }
        DLGPROC pProc = (DLGPROC)pThis->m_thunk.GetWndProc();"""
),
(
"""        if (bRet) {
            switch (uMsg) {
            case WM_COMPAREITEM:
            case WM_VKEYTOITEM:
            case WM_CHARTOITEM:
            case WM_INITDIALOG:
            case WM_QUERYDRAGICON:
            case WM_CTLCOLORMSGBOX:
            case WM_CTLCOLOREDIT:
            case WM_CTLCOLORLISTBOX:
            case WM_CTLCOLORBTN:
            case WM_CTLCOLORDLG:
            case WM_CTLCOLORSCROLLBAR:
            case WM_CTLCOLORSTATIC:
                // Return the result directly for these messages
                return (INT_PTR)lRes;
            default:
                break;
            }
            if (lRes != 0) {
                ::SetWindowLongPtr(pThis->m_hWnd, DWLP_MSGRESULT, lRes);
            }
            return TRUE;
        }

        if (uMsg == WM_NCDESTROY) {
            pThis->m_dwState |= CWindowImplRoot<TBase>::WINSTATE_DESTROYED;
        }

        if ((pThis->m_dwState & CWindowImplRoot<TBase>::WINSTATE_DESTROYED) && (pThis->m_pCurrentMsg == NULL)) {
            HWND hWndThis = pThis->m_hWnd;
            pThis->m_hWnd = NULL;
            pThis->m_dwState &= ~CWindowImplRoot<TBase>::WINSTATE_DESTROYED;
            pThis->OnFinalMessage(hWndThis);
        }

        return FALSE;""",
"""        // A nested WM_NCDESTROY may have happened while handling an
        // application message. Never return from the handled branch before
        // retiring its pending final notification.
        INT_PTR result = FALSE;
        if (bRet) {
            BOOL returnDirectly = FALSE;
            switch (uMsg) {
            case WM_COMPAREITEM:
            case WM_VKEYTOITEM:
            case WM_CHARTOITEM:
            case WM_INITDIALOG:
            case WM_QUERYDRAGICON:
            case WM_CTLCOLORMSGBOX:
            case WM_CTLCOLOREDIT:
            case WM_CTLCOLORLISTBOX:
            case WM_CTLCOLORBTN:
            case WM_CTLCOLORDLG:
            case WM_CTLCOLORSCROLLBAR:
            case WM_CTLCOLORSTATIC:
                returnDirectly = TRUE;
                break;
            default:
                break;
            }
            if (returnDirectly) {
                result = (INT_PTR)lRes;
            } else {
                if (lRes != 0 && pThis->m_hWnd != NULL && ::IsWindow(pThis->m_hWnd))
                    ::SetWindowLongPtr(pThis->m_hWnd, DWLP_MSGRESULT, lRes);
                result = TRUE;
            }
        }

        if (uMsg == WM_NCDESTROY) {
            pThis->m_dwState |= CWindowImplRoot<TBase>::WINSTATE_DESTROYED;
        }

        if ((pThis->m_dwState & CWindowImplRoot<TBase>::WINSTATE_DESTROYED) && (pThis->m_pCurrentMsg == NULL)) {
            HWND hWndThis = pThis->m_hWnd;
            pThis->m_hWnd = NULL;
            pThis->m_dwState &= ~CWindowImplRoot<TBase>::WINSTATE_DESTROYED;
            pThis->OnFinalMessage(hWndThis);
            // OnFinalMessage can delete pThis. Only return the local result.
        }
        return result;"""
),
(
"""    ThunkCode* pThunkCode;

    BOOL Init(WNDPROC proc, void* pThis) noexcept""",
"    // Windows OS calls remain REAL. Only OPENATL_WX_FAULT_TEST injects a\n    // deterministic failure before the named call; this is test-checkout\n    // instrumentation, never a shipped API or a production backend.\n#if defined(OPENATL_WX_FAULT_TEST)\n    inline static int testFailureStage = 0; // 1 alloc, 2 RW, 3 RX, 4 flush\n    inline static int testLivePages = 0;\n    inline static int testInjectedCount = 0;\n    inline static void* testLastAllocated = NULL;\n    inline static void* testLastReleased = NULL;\n    static void TestFailNext(int stage) noexcept {\n        testFailureStage = stage;\n        testInjectedCount = 0;\n        testLastAllocated = NULL;\n        testLastReleased = NULL;\n    }\n    static bool TestTakeFailure(int expected) noexcept {\n        if (testFailureStage != expected)\n            return false;\n        testFailureStage = 0;\n        ++testInjectedCount;\n        ::SetLastError(expected == 1 ? ERROR_OUTOFMEMORY : ERROR_ACCESS_DENIED);\n        return true;\n    }\n#endif\n    static void* ThunkAlloc(void* address, SIZE_T length, DWORD allocation,\n                            DWORD protection) noexcept {\n#if defined(OPENATL_WX_FAULT_TEST)\n        if (TestTakeFailure(1)) return NULL;\n#endif\n        void* result = ::VirtualAlloc(address, length, allocation, protection);\n#if defined(OPENATL_WX_FAULT_TEST)\n        if (result != NULL) {\n            ++testLivePages;\n            testLastAllocated = result;\n        }\n#endif\n        return result;\n    }\n    static BOOL ThunkProtect(void* address, SIZE_T length, DWORD protection,\n                             DWORD* prior) noexcept {\n#if defined(OPENATL_WX_FAULT_TEST)\n        if (protection == PAGE_READWRITE && TestTakeFailure(2)) return FALSE;\n        if (protection == PAGE_EXECUTE_READ && TestTakeFailure(3)) return FALSE;\n#endif\n        return ::VirtualProtect(address, length, protection, prior);\n    }\n    static BOOL ThunkFlush(HANDLE process, const void* address,\n                            SIZE_T length) noexcept {\n#if defined(OPENATL_WX_FAULT_TEST)\n        if (TestTakeFailure(4)) return FALSE;\n#endif\n        return ::FlushInstructionCache(process, address, length);\n    }\n    static BOOL ThunkFree(void* address) noexcept {\n        BOOL released = ::VirtualFree(address, 0, MEM_RELEASE);\n#if defined(OPENATL_WX_FAULT_TEST)\n        if (released) {\n            --testLivePages;\n            testLastReleased = address;\n        }\n#endif\n        return released;\n    }\n\n    ThunkCode* pThunkCode;\n\n    BOOL Init(WNDPROC proc, void* pThis) noexcept"
),
(
"""            ::VirtualFree(pThunkCode, 0, MEM_RELEASE);
            pThunkCode = NULL;
        }
    }
};""",
"""            ThunkFree(pThunkCode);
            pThunkCode = NULL;
        }
    }
};"""
),
]


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("Usage: python scripts/openatl-wx-hardening.py OPENATL_GIT_CHECKOUT")
    root = Path(sys.argv[1]).resolve(strict=True)
    git_head = subprocess.check_output(
        ["git", "-C", str(root), "rev-parse", "HEAD"], text=True
    ).strip()
    if git_head != PIN:
        raise SystemExit(f"Refuse non-pinned OpenATL HEAD {git_head}")
    header = root / "include" / "atlwin.h"
    contents = header.read_text(encoding="utf-8")
    for old, new in REPLACEMENTS:
        if contents.count(old) != 1:
            raise SystemExit("Refuse unrecognized/previously modified atlwin.h: " + old[:52])
        contents = contents.replace(old, new)
    if "PAGE_EXECUTE_READWRITE" in contents:
        raise SystemExit("Fail: CWndProcThunk still references RWX")
    if "0x58000090" not in contents or "PAGE_EXECUTE_READ" not in contents:
        raise SystemExit("Fail: ARM64 target or W^X thunk missing")
    header.write_text(contents, encoding="utf-8")
    print("Pinned OpenATL CWndProcThunk POC patched: RW -> RX, ARM64 literal, checked failure")


if __name__ == "__main__":
    main()
