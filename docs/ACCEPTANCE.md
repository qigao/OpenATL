# Required UI-only and optional COM Client acceptance

The only supported features are Win32/WTL UI **without COM initialization**
and an independently opted-in COM **client**. Native tests must verify
these paths separately on the exact branch HEAD.

| Gate | llvm-mingw Windows x64 | llvm-mingw Windows ARM64 | MSVC Windows x64 | MSVC Windows ARM64 | Linux/macOS |
| --- | --- | --- | --- | --- | --- |
| WTL10 hidden GUI, message map, resource dialog, W^X | 5× native, **no COM apartment** | 5× real native, **no COM apartment** | 5× native CTest, **no COM apartment** | 5× real native, **no COM apartment** | llvm-mingw PE build |
| COM Client `CoCreateInstance`, `CComPtr`, BSTR, Variant | 5× native | 5× real native | 5× native CTest | 5× real native | llvm-mingw PE build |
| Full official WTL10 SDI + .rc | PE + `.rsrc` link | PE machine/link | PE + `.rsrc` link | PE machine/link | llvm-mingw PE/link |
| COM Server, RGS, `OBJECT_ENTRY_AUTO`, registry writer | **Out of scope** | Out of scope | Out of scope | Out of scope | Out of scope |
| CFG/CET, DSO unload, interactive full SDI | Not qualified | Not qualified | Not qualified | Not qualified | N/A |

**Separation contracts**:
- WTL must not call `CoInitializeEx`; UI test checks
  `CoGetContextToken` reports `CO_E_NOTINITIALIZED` before and after
  window/dialog lifetime. This test links ole32 only for the assertion;
  the `OpenATL::WTL` interface target has **no COM link dependency**.
- A COM Client must initialize its own apartment, activate the Windows
  `CLSID_StdGlobalInterfaceTable` system component, query `IUnknown`,
  safely manage `CComPtr` ownership and release references before
  `CoUninitialize`. No app-owned COM server is needed.
- The exact pinned clean-room OpenATL header marker must be verified.
  MSVC's own ATL, WTL8 and RATL must never serve as fallbacks.
- Native ARM64 receives independently built PE files and SHA256
  provenance and executes on genuine Windows ARM64; cross-build != run.
- No required test may be skipped, disabled, or replaced by a fake pass.

**Draft / DO NOT MERGE.** Passing tests qualify the scoped behavior
only, not a distributable SDK or production GUI mitigation compliance.
