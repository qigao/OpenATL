# OpenATL native DLL unload and callback owner admission

This qualification uses real Win32 DLL load/unload, not a fake unload flag.
The OpenATL CWndProcThunk executes inside a dynamically loaded test DLL;
the host loads it with LoadLibrary, resolves exports, and holds it loaded
until a quiescent admission check has passed.

Acceptance conditions:
- A read/write gate rejects new message submissions before STOP admission.
- All successful PostMessage work is dispatched before the owner thread's
  STOP message; a single thread creates and destroys its HWND.
- Nested WM_NCDESTROY and OnFinalMessage complete exactly once, and no
  callback remains active after UI thread join.
- The thunk's VirtualAlloc page is released (VirtualQuery -> MEM_FREE).
- The previously DLL-associated window class is explicitly unregistered
  BEFORE the DLL is unmapped; reload must never reuse an old WndProc.
- All sender threads have exited the DLL export before FreeLibrary.
- GetModuleHandle confirms the DLL was unloaded and the next cycle can
  independently load and repeat all checks without a COM apartment.

Native test: 12 unload/reload rounds per run; four concurrent producer
threads overlapped with a bounded stop; five run repetitions per x64/ARM64
compiler configuration. Linux/macOS PE cross-compilation is not native
runtime evidence. A failed owner stop prohibits FreeLibrary.

This deliberately does NOT attempt an unsafe FreeLibrary while live code
is on another thread's stack, does not prove safe forced process teardown,
and is not a general hot-reload ABI for uncooperative third-party DLLs.
CFG/CET and interactive SDI have their own PRs under Issue #2.
