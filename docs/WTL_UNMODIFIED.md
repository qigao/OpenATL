# Original WTL 10 source compatibility (Issue #44)

This is a strict, **currently unqualified** feature-parity gate, independent
from the successful single-provider vcpkg transport integration (#43).

## Sources and ownership

The exact pinned `qigao/vcpkg-cache` `wtl@10.0.10320#6` package supplies
the original complete WTL headers, 11 original samples, AppWizard and
the upstream MS-PL license. OpenATL itself supplies all ATL/COM Client/
COM Server implementation under `include/`; Microsoft ATL is forbidden.

`OPENATL_BUILD_WTL_UNMODIFIED=ON` builds an additional native executable
using the absolute installed **original** `atlapp.h` path and the installed
WTL include directory *ahead of* OpenATL's presently adapted copy.
This probe must compile and run as real Windows code. It does not mutate
package files, rewrite original WTL header bodies, skip existing samples
or accept a second provider. The existing adapted qualification remains
an independent mandatory regression suite.

## Whole-project original header admission

`OPENATL_WTL_FULL_ORIGINAL=ON` is a separate, qualification-only option
that sets `OpenATL::ATL`'s WTL include path to the **exact installed vcpkg
WTL headers**, with zero `PrepareWtl.cmake` execution. It does not add
a fallback or second WTL source. The mandatory Win32 MSVC job now
reconfigures after the existing adapted x86 suite, rebuilds *all eleven
unchanged official WTL samples* plus native COM/UI tests, and repeats
native CTest five times. Any missing original ATL API, compiler
mismatch, or runtime failure is RED — do not silently return to the
adapted path or skip the original mode. The next strict llvm-mingw failure came from unchanged
`BmpView/props.h`: copy-initializing `WTL::CButton/CEdit` from
`ATL::CWindow` uses an MSVC conversion extension that Clang rejects.
OpenATL's MinGW-only `ATL::CWindow` now supplies a constrained conversion
operator to non-owning subclasses constructible from `HWND`. The original
WTL control and sample files remain unmodified; native original-WTL
tests check both borrowed control wrappers keep the same HWND identity.

The first strict llvm-mingw attempt diagnosed the SDK-level
`GetThemePartSize` mismatch: original WTL expects a `const RECT*`,
while current llvm-mingw declares a mutable `RECT*`. OpenATL's
MinGW-only `atlbase.h` now provides a C++ overload that copies the
read-only input rectangle before delegating to the real Win32 API,
without touching original WTL headers or creating a second UI provider.

The llvm-mingw i686 job now also runs the identical zero-adapter,
all-eleven-sample rebuild and 5x native CTest gate. WTL API/compiler
issues must be fixed in OpenATL or a separately reviewed upstream WTL
patch — never by deleting upstream WTL declarations.
A GREEN for this qualification is still not proof of original
`StartMonitor()` concurrent retirement or genuine PrintDlgEx printer UI behavior.

## First direct-source acceptance

* Restore `ATL::CComModule` and its correct class-object/lock/registry
  lifetime surface; do not implement a fake no-op provider.
* Keep `WTL::CAppModule` deriving from `ATL::CComModule`, with its public
  original message-loop and setting-change behavior.
* Keep `WTL::CServerAppModule`, including monitor/shutdown and COM server
  semantics, without deleting public declarations.
* Keep `WTL::CPrintDialogEx` and real `IPrintDialogCallback` /
  `IObjectWithSite` behavior. The direct original-header test now creates
  **WTL::CPrintDialogEx from the pinned vcpkg package** and checks COM
  QueryInterface identity, callback responses, null/unsupported site errors,
  retained site AddRef/Release and absence of an implicit COM apartment,
  without opening any system print UI. `openatl.com_site_no_apartment`
  independently tests the reusable `ATL::IObjectWithSiteImpl<T>`.
  Actual Win32 print-dialog display/selection lifecycle and WTL
  `CRegProperty`/hyperlink registry behavior remain independent unqualified
  functionality gates.
* Basic WTL UI must not silently initialize a COM apartment.

## Original WTL10 registry-property behavior (HKCU sandbox)

The original vcpkg `atlmisc.h` includes `WTL::CRegProperty` and
`CRegPropertyImpl<T>`. This Draft branch has now stopped deleting that
section from the build-local WTL adapter, alongside the original
`CPrintDialogEx` callback and HyperLink color registry handling.
The merged `master` remains unchanged. In this Draft branch,
`atlapp.h` is now byte-identical in the build-local include directory
and the installed original WTL package: no `CAppModule` inheritance
replacement, no `CServerAppModule` class deletion, and no
`ATL::_pAtlModule` owner rewrite. The checksum is rechecked even on a
warm CMake configure. Other narrower compiler-specific adapter edits
remain. Crucially, retaining an original public server class is **not**
proof that its detached `StartMonitor` thread can be safely retired. `reg_property.cpp` is compiled ONLY against the
original installed WTL source and calls `CRegProperty::WriteOne/ReadOne`
for DWORD, bool and counted Unicode text. Every native test round creates
a GUID-unique registry key under HKCU and verifies Win32 `REG_DWORD` values.
Original WTL10 `ReadOne` deliberately treats `ERROR_FILE_NOT_FOUND`
as an optional/default success without mutating the old destination value;
`ReadProp` still returns the precise `ERROR_FILE_NOT_FOUND` status,
and wrong types return `ERROR_INVALID_DATATYPE`. The test verifies both
paths and deletes the key it owns. No HKCR/HKLM writes or implicit COM.
The expanded direct-source native test also uses `CRegPropertyImpl<T>`
with unchanged `BEGIN_REGPROP_MAP`/`REG_PROPERTY` macros; checks OnRead/
OnWrite and precise OnReadError callbacks, missing-property defaults, and
propagation of valid neighboring fields. It exercises bounded `BinaryProp`
and `CharArrayProp` buffers, original `ERROR_OUTOFMEMORY` handling
without partial modification, and `HFONT` LOGFONT registry persistence
with explicitly owned GDI cleanup. The exact pinned vcpkg header source
is audited for those public member and boundedness contracts. Further
fault injection, genuine print-dialog UI and production adapter retirement
remain unqualified.

## Original WTL10 HyperLink registry color behavior

A dedicated native test mode (`--hyperlink`) uses
`RegOverridePredefKey(HKEY_CURRENT_USER, ownedSandbox)` within the
**test process only**. It writes two temporary registry values under that
isolated sandbox, creates and destroys a real original
`WTL::CHyperLink` child window, and checks that its
`m_clrLink`/`m_clrVisited` are the values parsed by the original WTL
`Init()` from the redirected Internet Explorer settings key. The
process-wide override is restored **before** closing and deleting the
GUID-owned sandbox key; the test never edits real IE settings or HKLM/HKCR.
The test also checks that original WTL does not initialize a COM apartment.
Native x86/x64 CTest runs this in a separate process; both Windows ARM64
test jobs execute the same mode five times.

## Original CServerAppModule shutdown monitor — bounded evidence

The unchanged WTL10 `CServerAppModule::StartMonitor()` launches a detached
monitor worker then immediately discards the thread handle. It provides
**no join proof**, and `m_bActivity` is a non-atomic field shared with
`Unlock()`. Therefore merely compiling or calling `StartMonitor()` is not
evidence of safe retirement. This path stays **UNQUALIFIED**, not silently
replaced with a legacy fallback or counted as passing.

The native `server_monitor.cpp` test instead launches **the original**
`CServerAppModule::MonitorProc` with a retained Windows thread handle,
using `CREATE_SUSPENDED`, one original shutdown event, and an explicit,
bounded join *before* `Term()`. The owner thread creates a message queue
before the monitor can post `WM_QUIT`. Cases cover idle exit, a held
module lock blocking retirement, explicit owner-controlled lock release,
original `CServerAppModule::Unlock()` event signaling **before** the monitor
is started, shutdown event disposal and the original `WM_QUIT` post. The owner only
writes `m_bActivity` before spawning the monitor; it intentionally uses
`CComModule::Unlock` plus an explicit event signal to avoid racing the
original non-atomic `CServerAppModule::Unlock`. A stuck worker fails
closed rather than destroying live memory. This test qualifies a
**quiescent, explicitly joined original monitor path**, not all original
`StartMonitor` and concurrent `Unlock` schedules.


The direct-source gate runs in the **normal full source CI** for
MSVC x86/x64/ARM64 and llvm-mingw x86/x64/ARM64, with Linux cross-build
qualification and SHA256/PE-verified, **real native Windows ARM64 execution**.
It is fail closed, never `continue-on-error`. Native CTest executes
`openatl.wtl_unmodified_no_com` five times with the remaining official suite.
It covers real legacy class-factory registration, COM activation, wrong-thread
and duplicate-registration rejection, cookie rollback and COM apartment ownership.
The production `OpenATL::ATL` target remains on its qualified adapted path
until original WTL COM server/registration, PrintDlgEx and registry functionality
is fully qualified on one exact HEAD.

## Reproduce

```sh
cmake --preset msvc-x64 -DOPENATL_BUILD_WTL_UNMODIFIED=ON
cmake --build --preset msvc-x64 --target openatl_wtl_unmodified_headers
ctest --preset msvc-x64 -R '^openatl.wtl_unmodified_no_com$' --output-on-failure
```

Provision the pinned vcpkg WTL dependency through
`qigao/vcpkg-cache` first, as described in the repository README.
