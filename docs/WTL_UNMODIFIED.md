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

## First direct-source acceptance

* Restore `ATL::CComModule` and its correct class-object/lock/registry
  lifetime surface; do not implement a fake no-op provider.
* Keep `WTL::CAppModule` deriving from `ATL::CComModule`, with its public
  original message-loop and setting-change behavior.
* Keep `WTL::CServerAppModule`, including monitor/shutdown and COM server
  semantics, without deleting public declarations.
* Keep `WTL::CPrintDialogEx` and actual `IPrintDialogCallback` /
  `IObjectWithSite` behavior. The new independent
  `openatl.com_site_no_apartment` test verifies COM AddRef/Release,
  interface-query error handling, self-assignment and no implicit apartment
  initialization for `ATL::IObjectWithSiteImpl<T>`. Original WTL dialog
  API admission remains blocked until the direct-header target passes.
  WTL `CRegProperty` and hyperlink registry behavior are subsequent
  explicit functionality gates.
* Basic WTL UI must not silently initialize a COM apartment.

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
