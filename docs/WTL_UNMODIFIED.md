# WTL 10.0.10320#7 — original vcpkg source only (Issue #44)

## Single source and runtime ownership

[WTL #7](https://github.com/qigao/vcpkg-cache/pull/237) is merged into
`qigao/vcpkg-cache master@8359cdbdf25c1f58cfc7230cc6318dc2fdb804fb`.
OpenATL's CMake and GitHub Actions pin that **exact commit**. The
original WTL release archive SHA512 and source revision remain fixed;
the port applies only narrowly audited Clang compatibility corrections.
All 20 public headers, 11 official examples, AppWizard and MS-PL are kept.

In this Draft branch, `OpenATL::ATL` obtains WTL headers **directly from
the installed vcpkg package** in every build. The build-local
`PrepareWtl.cmake` and copied/modified WTL header tree are retired.
`OPENATL_WTL_FULL_ORIGINAL=ON` is now the mandatory default, and
`OFF` fails rather than selecting an alternate provider. No Microsoft ATL,
SourceForge fallback or Installed OpenATL SDK exists.

## Verified original-source qualifications

- [CI #40846739457](https://github.com/qigao/OpenATL/actions/runs/40846739457):
  original WTL builds every unchanged official Win32 sample under MSVC
  and llvm-mingw, CTest x5.
- [CI #40850856167](https://github.com/qigao/OpenATL/actions/runs/40850856167):
  zero-adapter WTL native x64 CTest x5 on both MSVC and llvm-mingw.
- [CI #40852048002](https://github.com/qigao/OpenATL/actions/runs/40852048002):
  8/8 SUCCESS; original and adapted ARM64 artifacts independently verify
  SHA256/PE/resources and real Windows ARM64 UI, COM and native lifecycles
  x5 each for MSVC and llvm-mingw.
- This Draft's new default-original build must pass the same complete
  CI matrix again, with no skipped tests or rewritten source APIs.

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

## Remaining real product blockers — PR #45 stays Draft

The original `WTL::CServerAppModule::StartMonitor()` discards its
worker handle. Its non-atomic `m_bActivity`, concurrent
`Unlock()/Term()`, shutdown event lifetime and worker ownership do not
provide a safe join or unload proof. Existing `server_monitor.cpp` only
qualifies an explicitly retained worker with bounded join, not the
original detached StartMonitor/Unlock/Term schedules.

The actual Win32 `PrintDlgEx` printer-window and selection lifecycle
also remains unqualified; COM callback/source tests cannot stand in
for real printer UI acceptance. These two gates remain open under
[Issue #44](https://github.com/qigao/OpenATL/issues/44); the Draft
PR is not to be merged or released on source-build evidence alone.

## Reproduce

Install `wtl@10.0.10320#7` from the fixed vcpkg-cache overlay, then
set `VCPKG_INSTALLED_DIR`/`VCPKG_TARGET_TRIPLET` (or the OpenATL
source-tree env equivalents) and run:

```powershell
cmake --preset msvc-x64 -DOPENATL_BUILD_WTL_UNMODIFIED=ON
cmake --build --preset msvc-x64 --parallel 2
ctest --preset msvc-x64 --repeat until-fail:5 --output-on-failure
```

## Original ARM64 provenance contract (strict version 1)

Each independently compiled `*-original` ARM64 artifact now carries a
versioned `manifest.json` with an exact `provenance` object and a separate
`files` SHA256 map. The producer reads the **installed** WTL port identity
stamp after CMake's source gate and records the checked-out CI commit,
compiler (`llvm` or `msvc`), WTL version, provider, and archive SHA512.

On the **real Windows ARM64** runner, the existing five-pass test suite now
rejects missing/extra files, incorrect digest syntax, duplicate manifest
keys, missing/wrong source metadata, a different checkout, or cross-toolchain
artifact substitution *before executing any binary*. The legacy flat
manifest format remains valid only for the non-`original` artifact set.
The source-only job runs six negative-contract tests without claiming
printer UI or detached StartMonitor ownership is qualified.

## Exact PR HEAD checkout (not the synthetic merge ref)

GitHub Actions `pull_request` events ordinarily set `GITHUB_SHA` to a
synthetic merge commit. All seven checkout steps in this workflow now pin
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
Those remain a hard Draft blocker in Issue #44.

## 2026-10-29 hosted HSP preset regression (CI #40889043223)

MSVC x64 native WTL tests passed, but the second `cmake --preset msvc-x64`
reconfiguration re-applied the preset's intentional
`OPENATL_REQUIRE_CET_RUNTIME=ON` default after the first hosted invocation
had overridden it to OFF. The hosted GitHub Windows image reported
`parent_enabled=0`: **a genuine HSP-enforcement qualification failure**,
not a WTL compilation failure. The second hosted reconfigure now explicitly
sets OFF, checks the exact CMakeCache value, and logs HSP as **UNQUALIFIED**.
The canonical `msvc-x64` preset still defaults to ON, and
`.github/workflows/openatl-hsp-dedicated.yml` still requires real hardware
HSP ten times under each compiler; no HSP bypass or test removal is accepted.

## PrintDlgEx Win32 default-printer probe — not a modal UI qualification

The direct-original-header test now calls `WTL::CPrintDialogEx::GetDefaults()`,
which executes the genuine Win32 `PrintDlgEx` with `PD_RETURNDEFAULT`
but **without** the optional driver-dependent `PD_RETURNDC`. A separate
`GetDefaultPrinterW` call distinguishes a configured default printer from
a hosted machine with none. Where one exists, the test validates Unicode
`DEVNAMES` and `DEVMODE` bounds, manual `GlobalFree`, and explicit STA
COM shutdown. Device-context allocation and `DeleteDC` require a separate
real printer-enabled gate and are not inferred from successful metadata
retrieval. Where no
default exists it requires a failed Win32 call and emits
`WTL_PRINTER_READINESS default=0 ... modal=UNQUALIFIED` rather than inventing
a printer or accepting false success. Unexpected errors with a configured
printer are real REDs. This test is included in native CTest and the real
ARM64 five-pass suite, with strictly parsed two-line readiness diagnostics.

**Still unqualified:** visible `PrintDlgEx::DoModal()` property-sheet
creation, actual printer selection/cancellation, callback sequencing, and
modal window/resource retirement on a printer-enabled native runner. No
fake printer, WTL source edit, static stub or second printing provider.

### Hosted printer driver discovery — CI #40891861080

On the generic Windows x64 runner, `GetDefaultPrinterW` reported a
configured default printer, but the first probe's combined
`PD_RETURNDEFAULT | PD_RETURNDC` call returned
`0x80070006` (`ERROR_INVALID_HANDLE`). This is a real RED, not a
passing printer DC claim. The metadata-only probe now omits the optional
`PD_RETURNDC` flag and retains strict Win32 HRESULT and bounds checks.
A native printer-DC and modal UI test must still require an actual
functional printer driver; its absence remains **UNQUALIFIED**.

## Strict Win32 PrintDlgEx reference and HWND owner

Microsoft PRINTDLGEXW requires a **valid hwndOwner** even with non-modal
PD_RETURNDEFAULT. A console process generally has no active window; the
original WTL GetDefaults() fallback to GetActiveWindow() may yield NULL
and HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE). The earlier probe also
removed original WTL PD_NOPAGENUMS while leaving the page-range fields
uninitialized, violating another documented input contract. These were
**test fixture errors**, not evidence that ATL/WTL needed an API deletion.

The revised fixture creates a real hidden Win32 owner window (and destroys it
before COM apartment teardown), retains the original WTL page-range exclusion
flags, and verifies that GetDefaults() clears PD_RETURNDEFAULT on return,
as upstream actually does. The same STA then calls an independently initialized
Win32 PRINTDLGEXW with the same owner, flags and starting page. Both calls must
return the *same HRESULT* and, if successful, each must have valid, bounded
DEVNAMES/DEVMODE with all allocated handles freed.

A consistent failure is recorded explicitly as
`metadata=UNQUALIFIED` and qualifies only **WTL-to-Win32 API parity**,
not a functioning printer, native DC, or interactive DoModal. A mismatch
or malformed successful result is a mandatory test RED. A native
printer-backed DC and modal UI admission remains required under Issue #44.
