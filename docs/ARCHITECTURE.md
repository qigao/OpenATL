# OpenATL UI and COM architecture

UI/client paths share repository-owned clean-room ATL headers. An explicit
server API lives separately under `include/openatl/`:

```
Win32 GUI application
  └─ OpenATL::ATL ──> in-repository ATL + build-local adapted WTL headers
       (CMake fetches the pinned release; no consumer ports or COM initialization)

Optional system COM consumer
  └─ OpenATL::ATL ──> Windows ole32 / oleaut32 / uuid / advapi32
       (caller owns CoInitializeEx / CoUninitialize; CLIENT ONLY)

Optional in-process COM server / explicit registration
  └─ OpenATL::ATL ──> include/openatl/ headers + the same system libraries
```

`OpenATL::ATL` is the single header-only consumer build target for UI, COM
client, server and registration APIs. It exists only within a source-tree
CMake build. OpenATL **never** defines
`install()`, `export()`, a package config, or an Installed SDK. Consumers
build from the repository checkout; a post-install consumption path is
permanently excluded.
Optional CImage BMP/PNG file and caller-owned `IStream*` codec calls use Windows GDI+ with a per-call startup/shutdown scope and explicit `gdiplus` linkage by the consumer. Stream I/O never initializes a COM apartment or takes ownership of the stream; the caller handles positioning and release. File Load opens its Win32 input handle once and copies at most 64 MiB into an OS-owned HGLOBAL stream before invoking GDI+; the same bytes whose size was checked are decoded, and the file handle is closed before returning. Stream Load snapshots up to 64 MiB with exact HRESULT/byte-count checks before GDI+ decoding. Where supported, Stat and Seek(CUR, 0) also preflight the 64 MiB remaining-body limit before reading; unseekable streams remain supported via the byte counter. DIB Create and decoded GDI+ images have a separate 128 MiB pixel budget including 24bpp DIB row padding, enforced before OpenATL bitmap allocation or LockBits; this does not bound GDI+'s internal parser allocations: it accepts S_FALSE at EOF or a short S_OK only when Stat plus a non-moving Seek(CUR, 0) prove the caller stream reached EOF. Without EOF evidence short S_OK fails STG_E_READFAULT; read failures propagate their original HRESULT. Failed reads may advance the caller's position but never install a partial HBITMAP. For saving, an OS-created temporary HGLOBAL IStream holds the completed PNG/BMP encoding. OpenATL copies it in 8 KiB chunks to the caller's stream, rejecting short S_OK writes and propagating failing HRESULTs; partial caller-stream output can remain on failure, but there is no claim of transactional rollback. The image codec consumer explicitly links `gdiplus`; `ole32` is supplied by `OpenATL::ATL`. GDI+ is not a transitive `OpenATL::ATL` or WTL dependency and does not initialize a COM apartment. A failed decode never replaces an existing bitmap; invalid save formats are rejected before writing.

The WTL10 build-local adapter preserves Win32 dialogs, in-memory dialog resources,
MRU menus with optional HKCU persistence, file finding and message loops.
Registry persistence is provided by source-owned Win32 `ATL::CRegKey`, not
COM registration. The adapter removes the separate `CRegProperty`
serializer, legacy Internet Explorer color lookup and COM print callbacks.
ActiveX hosting is excluded at compile time without fake initialization.

Win32 UI can use WTL windowing, dialogs, message maps and common controls
without COM initialization. Registry writes occur only when explicitly
using application settings or MRU persistence; they require `advapi32` at
link time, not a COM Server or COM apartment. `CPath`, `CAtlFile`,
`CString` and native Win32 synchronization live in `include/` and do not
initialize COM. All consumers use the same `OpenATL::ATL` target and its
system link dependencies.

The production header surface contains the real Win32 base and window
modules (`_AtlBaseModule`, `_AtlWinModule`), optional `atlcomcli.h`
helpers and ordinary Win32 `atlreg.h` settings operations. The optional `atlcom.h` supplies nonaggregated ATL object templates and
COM-map macros, backed by native identity/lifetime tests. Obsolete `CComModule`,
auto object maps, ATL registrars, registration success stubs and fake ActiveX
initialization remain absent. A separate source policy rejects those legacy APIs;
compilation with pinned official WTL10 headers and the native UI/COM-client
tests is the actual acceptance gate.

**Excluded features:** legacy ATL server modules and object-map macros,
RGS, EXE servers and ActiveX/OLE container implementations. The separate
server API supports nonaggregated in-process servers through explicit C++
class/interface descriptors, with explicit CLSID/ProgID registration. These headers and the `atlcom.h` compatibility subset use the same
`OpenATL::ATL` target. See [ATL compatibility](ATL_COMPATIBILITY.md). EXE servers, RGS and ActiveX remain excluded.

The server neither initializes COM nor inherits the WTL UI module. A module
reference pins each object/factory, and balanced server locks independently
prevent unload. Explicit registration refuses existing keys and validates
ownership before nonrecursive deletion. Its root handle is supplied by the
application; tests use private HKCU roots and a uniquely identified COM class.
See [COM server API, ownership and registration](COM_SERVER.md).

## Upstream pins and licenses

- Clean-room MIT ATL source under `include/`, originally derived from
  `kkhaike/openatl@7b587f3e10f3f8d93a01522033de80007368bad5`.
  CI only compiles the checked-in headers and refuses an external ATL root.
- WTL10 MS-PL source is fetched by `cmake/OpenATLWTL.cmake`, pinned to the
  official 10.0.10320 release and SHA512. `cmake/wtl/PrepareWtl.cmake` adapts
  a build-local copy and retains upstream bug 329/issue 66 header fixes.
  Its license is copied beside the adapted headers. No source is vendored
  and no downstream vcpkg overlay or Microsoft ATL package is needed.
- WTL sample `kkhaike/wtltest@63ba6a992cb3d79d56be66cd283f6615c6646b3d`.
- Project-specific glue/tests use Apache-2.0. RATL references, RGS scripts
  and LGPL/GPL patches have been removed from the active code path and files.
  Historical Git commits preserve earlier research, not project scope.
- Both llvm-mingw 20261020 and genuine MSVC 2022 build and test Windows
  x64+ARM64; CMake manages WTL on all toolchains, including Linux/macOS
  Windows cross-builds. Each build tree owns its adapted headers.
  No Microsoft ATL include-path fallback.

The W^X thunk and test-only fault injection live in the checked-in
`atlwin.h`; no external checkout is patched. Actual CFG/CET
mitigations, callback teardown/unload races, interactive SDI and
production distribution are tracked separately.

See [toolchain matrix](TOOLCHAIN_MATRIX.md).
