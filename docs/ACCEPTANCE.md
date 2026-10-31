# Required ATL COM Client, COM Server and WTL compatibility acceptance

OpenATL must support **both** ATL COM Client and COM Server, plus Win32/WTL
windowing. Ordinary UI can run without initializing COM; actual COM features
need appropriate apartment lifecycle. Native tests verify these paths separately
on the exact branch HEAD.

| Gate | llvm-mingw Windows x64 | llvm-mingw Windows ARM64 | MSVC Windows x64 | MSVC Windows ARM64 |
| --- | --- | --- | --- | --- |
| WTL10 hidden GUI, message map, resource dialog, W^X | 5× native, **no COM apartment** | 5× real native, **no COM apartment** | 5× native CTest, **no COM apartment** | 5× real native, **no COM apartment** |
| COM Client `CoCreateInstance`, `CComPtr`, BSTR/VARIANT ownership, BSTR alias/overflow/embedded NUL, QI failure, self-Detach, locked-SAFEARRAY clear/assignment | 5× native | 5× real native | 5× native CTest | 5× real native |
| Native `CRegKey` HKCU DWORD/REG_SZ/REG_EXPAND_SZ/binary, invalid type, malformed nonterminated strings, capacity bounds, handle ownership and cleanup; WTL MRU menu ordering/dedup/roundtrip | 5× native, **no COM apartment** | 5× real native, **no COM apartment** | 5× native CTest, **no COM apartment** | 5× real native, **no COM apartment** |
| In-tree file/string/sync utility tests | 5× native | 5× real native | 5× native CTest | 5× real native |
| In-tree Unicode `CPath` + Win32 file test | 5× native | 5× real native | 5× native CTest | 5× real native |
| Counted UTF-8/UTF-16/ACP, malformed/lossy input, embedded NULs, checked CString/BSTR assignment and UTF-8 ACP manifest | 5× native | 5× real native | 5× native | 5× real native |
| Source-owned ATL heap, FILETIME, typed array and linked-list (`POSITION`, swap, ownership, throwing copy) | 5× native, **no COM apartment** | 5× real native | 5× native CTest | 5× real native |
| Win32 `CAtlFileMapping`: unaligned offsets, file/writeback, duplicate section ownership, named shared memory | 5× native, **no COM apartment** | 5× real native | 5× native CTest | 5× real native |
| Win32 `CImage`: 24/32-bpp DIB, HBITMAP/DC lifecycle, BMP/PNG file and caller-owned IStream Save/Load, BGRA alpha, unsupported GIF, fail-closed short/zero IStream writes, failing reads on first/second chunk, unproven short S_OK reads, verified HGLOBAL-style EOF, premature EOF, rejected empty input, 64 MiB one-open file snapshot and IStream preflight, 128 MiB decoded budget including 24bpp row padding, exclusive handle-release checks | 5× native, **no COM apartment** | 5× real native | 5× native CTest | 5× real native |
| Community WTL10 SDI fixture + .rc | PE + `.rsrc` link | PE machine/link | PE + `.rsrc` link | PE machine/link |
| Official WTL GuidGen | 5× native GUI behavior | 5× real native GUI behavior | 5× native GUI behavior | 5× real native GUI behavior |
| Official WTL MemDlg | Blocked: llvm-rc TOOLBAR syntax | Blocked: llvm-rc TOOLBAR syntax | 5× native resource/memory dialog behavior | 5× real native dialog behavior |
| CAtlExeModuleT lifecycle, raw registry QueryValue and three unchanged RATL samples | 5× native module + sample behavior | Native module; sample PE build | 5× native module + sample behavior | Native module; sample PE build |
| Optional explicit COM server: interface identity, class factories, module locks, constructor rollback, registry ownership/conflicts, real STA/MTA DLL activation and unregistration | 5× native | 5× real native | 5× native | 5× real native |
| Standard ATL COM Server DLL modules, automatic/chained object maps, RGS, Automation/type libraries and SAFEARRAY | **Compatibility gaps; implementation required** | Not qualified | Not qualified | Not qualified |
| CFG/CET, DSO unload, interactive full SDI | Image CFG/CET + native unload/SDI, no hosted HSP claim | Native CFG/unload/SDI | Native CFG/unload/SDI; no hardware HSP test | Native CFG/unload/SDI |
| ATL-compatible nonaggregated object creation, COM maps, canonical identity, FinalConstruct/FinalRelease and explicit module pins | 5× native | 5× real native | 5× native | 5× real native |

**Separation contracts**:
- WTL UI must not call `CoInitializeEx`; the native UI test checks
  `CoGetContextToken` before and after dialog lifetime. `OpenWTL::WTL`
  provides the native WTL frontend and the private ATL/COM backend
  transitively, without implicitly initializing a COM apartment.
- A COM Client must initialize its own apartment, activate the Windows
  `CLSID_StdGlobalInterfaceTable` system component, query `IUnknown`,
  safely manage `CComPtr` ownership and release references before
  `CoUninitialize`. No app-owned COM server is needed.
- Official WTL headers and original resources must come exclusively
  from the complete `wtl/` source tree and its 296-file byte manifest.
  No installed WTL package, copied adapter or alternate implementation.
  Existing GUI/COM/ARM64 native tests remain mandatory for both toolchains.
- The checked-in clean-room OpenATL header marker must be verified at
  CMake configuration and compile time. No external OpenATL checkout is used.
  MSVC's own ATL, WTL8 and RATL must never serve as fallbacks.
- Native ARM64 receives independently built PE files and SHA256
  provenance and executes on genuine Windows ARM64; a Windows x64→ARM64 build is not a native ARM64 run.
- No required test may be skipped, disabled, or replaced by a fake pass.

Real printer/PrintDlgEx device probes and hardware HSP enforcement are outside
CI by owner decision (2026-10-31), including manual GitHub Actions workflows.
They are not merge or feature-completion requirements. Device-independent
printing COM callback/site tests and CFG/CET PE metadata checks remain in scope.

**Scope qualification:** passing the exact-head Windows x64/ARM64 native tests qualifies the checked-in source-tree APIs, not production CFG/CET mitigation compliance. The PR may be reviewed for merge separately; this document does not authorize an automatic merge.

**Current build status:** no installed/exported OpenATL SDK or post-install
consumer acceptance exists yet. This is not a permanent exclusion:
packaging/exports can be designed under #4 with native client/server validation.
