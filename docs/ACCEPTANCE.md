# Required ATL COM Client, COM Server and WTL compatibility acceptance

OpenATL must support **both** ATL COM Client and COM Server, plus Win32/WTL
windowing. Ordinary UI can run without initializing COM; actual COM features
need appropriate apartment lifecycle. Native tests verify these paths separately
on the exact branch HEAD.

| Gate | llvm-mingw Windows x64 | llvm-mingw Windows ARM64 | MSVC Windows x64 | MSVC Windows ARM64 | Linux/macOS |
| --- | --- | --- | --- | --- | --- |
| WTL10 hidden GUI, message map, resource dialog, W^X | 5× native, **no COM apartment** | 5× real native, **no COM apartment** | 5× native CTest, **no COM apartment** | 5× real native, **no COM apartment** | llvm-mingw PE build |
| COM Client `CoCreateInstance`, `CComPtr`, BSTR/VARIANT ownership, BSTR alias/overflow/embedded NUL, QI failure, self-Detach, locked-SAFEARRAY clear/assignment | 5× native | 5× real native | 5× native CTest | 5× real native | llvm-mingw PE build |
| Native `CRegKey` HKCU DWORD/REG_SZ/REG_EXPAND_SZ/binary, invalid type, malformed nonterminated strings, capacity bounds, handle ownership and cleanup; WTL MRU menu ordering/dedup/roundtrip | 5× native, **no COM apartment** | 5× real native, **no COM apartment** | 5× native CTest, **no COM apartment** | 5× real native, **no COM apartment** | llvm-mingw PE build |
| In-tree file/string/sync utility tests | 5× native | 5× real native | 5× native CTest | 5× real native | llvm-mingw PE build |
| In-tree Unicode `CPath` + Win32 file test | 5× native | 5× real native | 5× native CTest | 5× real native | llvm-mingw PE build |
| Counted UTF-8/UTF-16/ACP, malformed/lossy input, embedded NULs, checked CString/BSTR assignment and UTF-8 ACP manifest | 5× native | 5× real native | 5× native | 5× real native | PE/resource build |
| Source-owned ATL heap, FILETIME, typed array and linked-list (`POSITION`, swap, ownership, throwing copy) | 5× native, **no COM apartment** | 5× real native | 5× native CTest | 5× real native | llvm-mingw PE build |
| Win32 `CAtlFileMapping`: unaligned offsets, file/writeback, duplicate section ownership, named shared memory | 5× native, **no COM apartment** | 5× real native | 5× native CTest | 5× real native | llvm-mingw PE build |
| Win32 `CImage`: 24/32-bpp DIB, HBITMAP/DC lifecycle, BMP/PNG file and caller-owned IStream Save/Load, BGRA alpha, unsupported GIF, fail-closed short/zero IStream writes, failing reads on first/second chunk, unproven short S_OK reads, verified HGLOBAL-style EOF, premature EOF, rejected empty input, 64 MiB one-open file snapshot and IStream preflight, 128 MiB decoded budget including 24bpp row padding, exclusive handle-release checks | 5× native, **no COM apartment** | 5× real native | 5× native CTest | 5× real native | llvm-mingw PE build |
| Community WTL10 SDI fixture + .rc | PE + `.rsrc` link | PE machine/link | PE + `.rsrc` link | PE machine/link | llvm-mingw PE/link |
| Official WTL GuidGen | 5× native GUI behavior | 5× real native GUI behavior | 5× native GUI behavior | 5× real native GUI behavior | PE/resource build |
| Official WTL MemDlg | Blocked: llvm-rc TOOLBAR syntax | Blocked: llvm-rc TOOLBAR syntax | 5× native resource/memory dialog behavior | 5× real native dialog behavior | Not admitted |
| CAtlExeModuleT lifecycle, raw registry QueryValue and three unchanged RATL samples | 5× native module + sample behavior | Native module; sample PE build | 5× native module + sample behavior | Native module; sample PE build | Sample/module PE/resource build |
| Optional explicit COM server: interface identity, class factories, module locks, constructor rollback, registry ownership/conflicts, real STA/MTA DLL activation and unregistration | 5× native | 5× real native | 5× native | 5× real native | server DLL/client PE build |
| Standard ATL COM Server DLL modules, automatic/chained object maps, RGS, Automation/type libraries and SAFEARRAY | **Compatibility gaps; implementation required** | Not qualified | Not qualified | Not qualified | Admit original Microsoft ATL samples and native activation/registration after implementation; never fake a pass |
| CFG/CET, DSO unload, interactive full SDI | Image CFG/CET + native unload/SDI; HSP excluded on hosted workers | Native CFG/unload/SDI | Native CFG/HSP/unload/SDI | Native CFG/unload/SDI | PE/resource checks |
| ATL-compatible nonaggregated object creation, COM maps, canonical identity, FinalConstruct/FinalRelease and explicit module pins | 5× native | 5× real native | 5× native | 5× real native | PE build |

**Separation contracts**:
- WTL must not call `CoInitializeEx`; UI test checks
  `CoGetContextToken` reports `CO_E_NOTINITIALIZED` before and after
  window/dialog lifetime. This test links ole32 only for the assertion;
  WTL comes from CMake FetchContent without introducing a COM apartment or a synthetic
  `OpenATL::WTL` interface target. The unified `OpenATL::ATL` target supplies
  COM/registry link dependencies without initializing an apartment.
- A COM Client must initialize its own apartment, activate the Windows
  `CLSID_StdGlobalInterfaceTable` system component, query `IUnknown`,
  safely manage `CComPtr` ownership and release references before
  `CoUninitialize`. No app-owned COM server is needed.
- Official WTL headers must come from the checksum-pinned CMake dependency
  and build-local adapter. UI targets inherit headers and link dependencies
  from `OpenATL::ATL`; no consumer overlay, WTL install or sample patch.
- The checked-in clean-room OpenATL header marker must be verified at
  CMake configuration and compile time. No external OpenATL checkout is used.
  MSVC's own ATL, WTL8 and RATL must never serve as fallbacks.
- Native ARM64 receives independently built PE files and SHA256
  provenance and executes on genuine Windows ARM64; cross-build != run.
- No required test may be skipped, disabled, or replaced by a fake pass.

**Scope qualification:** passing the exact-head native and cross-platform tests qualifies the checked-in source-tree APIs, not production CFG/CET mitigation compliance. The PR may be reviewed for merge separately; this document does not authorize an automatic merge.

**Current build status:** no installed/exported OpenATL SDK or post-install
consumer acceptance exists yet. This is not a permanent exclusion:
packaging/exports can be designed under #4 with native client/server validation.
