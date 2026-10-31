# WTL 10.0.10320 — repository source and original API acceptance (Issue #44)

## One source and internal ATL backend

The complete WTL 10.0.10320 release is checked in at `wtl/`: all
20 original public headers, 11 original Samples, AppWizard and MS-PL.
Seven previously qualified vcpkg compatibility fixes are applied in
place, with all 296 files inventoried by `wtl/source-manifest.json`.

Consumers link `OpenWTL::WTL`; the `openatl_atl` backend remains
internal and provides ATL/COM. No public `OpenATL::ATL` alias,
installed WTL source, adapted header copy or fallback remains.
`OPENATL_WTL_FULL_ORIGINAL=ON` is mandatory; OFF fails closed.

## Verified original-source qualifications

- [CI #40846739457](https://github.com/qigao/OpenWTL/actions/runs/40846739457):
  original WTL builds every unchanged official Win32 sample under MSVC
  and llvm-mingw, CTest x5.
- [CI #40850856167](https://github.com/qigao/OpenWTL/actions/runs/40850856167):
  zero-adapter WTL native x64 CTest x5 on both MSVC and llvm-mingw.
- [CI #40852048002](https://github.com/qigao/OpenWTL/actions/runs/40852048002):
  8/8 SUCCESS; original and adapted ARM64 artifacts independently verify
  SHA256/PE/resources and real Windows ARM64 UI, COM and native lifecycles
  x5 each for MSVC and llvm-mingw.
- Current PRs must pass the **Windows x64/ARM64** MSVC and llvm-mingw native
  CI matrix. Historical x86 full-example evidence remains available, but
  is no longer part of mandatory CI under the narrowed target policy.

The optional `OPENATL_BUILD_WTL_UNMODIFIED=ON` executable adds
`CComModule` legacy class-object ownership and COM registration/rollback,
original `CAppModule` message loops, `CPrintDialogEx` COM callback/site,
`CRegPropertyImpl<T>` DWORD/bool/Unicode/binary/char-array/HFONT
serialization and bounded buffer errors, and genuine `CHyperLink`
Win32 controls. Isolated HKCU registry testing uses
`RegOverridePredefKey` only in the test process and restores it
before deleting its GUID-owned key.

`tests/wtl10/original_arm64_manifest.py` refuses a non-original
CMake cache or copied WTL headers, validates the ARM64 PE and resources,
and produces exact SHA256 provenance for the separately built binaries.
`native_arm64.py` refuses non-native Windows ARM64 runners.

## Remaining native monitor behavior — Issue #44

The original `WTL::CServerAppModule::StartMonitor()` discards its
worker handle. Its non-atomic `m_bActivity`, concurrent
`Unlock()/Term()`, shutdown event lifetime and worker ownership do not
provide a safe join or unload proof. Existing `server_monitor.cpp` only
qualifies an explicitly retained worker with bounded join, not the
original detached StartMonitor/Unlock/Term schedules.

Real printer-backed PrintDlgEx UI/DC behavior and hardware HSP enforcement
are outside CI by owner decision (2026-10-31). They do not block this milestone
or PR merges. Device-independent PrintDlgEx COM callback/site tests remain in
native CI; Issue #44 now tracks the monitor-worker lifecycle only.

## Reproduce

Use a pinned Windows MSVC/llvm-mingw toolchain; WTL needs no vcpkg install:

```powershell
cmake --preset msvc-x64 -DOPENATL_BUILD_WTL_UNMODIFIED=ON
cmake --build --preset msvc-x64 --parallel 2
ctest --preset msvc-x64 --repeat until-fail:5 --output-on-failure
```

## Original ARM64 provenance contract (strict version 1)

Each independently compiled `*-original` ARM64 artifact now carries a
versioned `manifest.json` with an exact `provenance` object and a separate
`files` SHA256 map. The producer verifies the **repository-owned** 296-file WTL source
manifest after CMake's source gate and records the checked-out CI commit,
compiler (`llvm` or `msvc`), WTL version, provider, and archive SHA512.

On the **real Windows ARM64** runner, the existing five-pass test suite now
rejects missing/extra files, incorrect digest syntax, duplicate manifest
keys, missing/wrong source metadata, a different checkout, or cross-toolchain
artifact substitution *before executing any binary*. The legacy flat
manifest format remains valid only for the non-`original` artifact set.
The source-only job runs negative-contract tests without claiming
printer UI or detached StartMonitor ownership is qualified.

## Exact PR HEAD checkout (not the synthetic merge ref)

GitHub Actions `pull_request` events ordinarily set `GITHUB_SHA` to a
synthetic merge commit. All five checkout steps in the current workflow pin
`github.event.pull_request.head.sha` (and `github.sha` for push/manual
events). The producer and actual Windows ARM64 runners both read
`git rev-parse HEAD`, compare it to
`OPENATL_EXPECTED_CHECKOUT_SHA`, and fail closed on missing or mismatched
commit IDs. Original-WTL artifact provenance therefore refers to the
**actual PR commit**, not simply an event-provided merge SHA.

## Unmodified `StartMonitor()` process-only behavior test

An additional native CTest and five-pass Windows ARM64 acceptance execute the
**actual upstream** `WTL::CServerAppModule::StartMonitor()`, not the directly
called `MonitorProc()`. The test starts with a stable, quiescent module,
creates the owner thread message queue, signals the shutdown event, and checks
that the original worker posts `WM_QUIT` within a fixed deadline. It never
races the upstream non-atomic `m_bActivity` with concurrent `Unlock()`.

Because upstream `StartMonitor()` immediately discards the worker's thread
handle, the fixture is confined to a separate process and calls
`ExitProcess` rather than running `Term()` against a potentially live
worker. **This proves only real API startup/idle notification**, not a
join, concurrent shutdown, safe DLL unload, or full COM server retirement.
Those remain implementation and native lifetime work in Issue #44.

## Optional local printer diagnostics — outside CI

The existing executable retains an explicit manual mode:

```powershell
.\build-msvc-x64\openatl_wtl_unmodified_headers.exe --print-defaults
```

It creates a real HWND owner and compares the original WTL GetDefaults()
call with Win32 PrintDlgExW using matching flags. Successful metadata must have
bounded DEVNAMES/DEVMODE and correct cleanup. This is an optional diagnostic;
CTest and the real ARM64 runner never invoke it. It does not qualify modal
selection/cancel, a printer DC or a functioning printer driver.

The normal mode continues to verify original PrintDlgEx COM callback/site
identity and ownership without opening a printer or querying printer devices.
Hardware diagnostics likewise remain explicit local invocations only; see
[CET_HARDWARE_ACCEPTANCE.md](CET_HARDWARE_ACCEPTANCE.md).
