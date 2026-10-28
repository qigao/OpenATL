# OpenATL ATL compatibility architecture

OpenATL implements both **COM Client and COM Server** in the same source-owned
ATL-compatible library, together with Win32/WTL windowing. The existing
`openatl/` explicit server API and `atlcom.h` compatibility templates are
foundation code, not an out-of-scope extension. See
[canonical project scope](PROJECT_SCOPE.md). Current integration:

```
Win32 GUI application
  └─ OpenATL::ATL ──> in-repository ATL + build-local adapted WTL headers
       (CMake fetches the pinned release; no consumer ports or COM initialization)

Optional system COM consumer
  └─ OpenATL::ATL ──> Windows ole32 / oleaut32 / uuid / advapi32
       (caller explicitly manages the COM apartment)

In-process COM server / class factory / registration
  └─ OpenATL::ATL ──> include/openatl/ headers + the same system libraries
```

`OpenATL::ATL` is currently the source-tree CMake target for Win32 UI,
COM Client, COM Server and registration APIs. Installed/exported integration
is not yet implemented; future packaging is evaluated in issue #4, rather
than forbidden by a permanent project constraint.
Optional CImage BMP/PNG file and caller-owned `IStream*` codec calls use Windows GDI+ with a per-call startup/shutdown scope and explicit `gdiplus` linkage by the consumer. Stream I/O never initializes a COM apartment or takes ownership of the stream; the caller handles positioning and release. File Load opens its Win32 input handle once and copies at most 64 MiB into an OS-owned HGLOBAL stream before invoking GDI+; the same bytes whose size was checked are decoded, and the file handle is closed before returning. Stream Load snapshots up to 64 MiB with exact HRESULT/byte-count checks before GDI+ decoding. Where supported, Stat and Seek(CUR, 0) also preflight the 64 MiB remaining-body limit before reading; unseekable streams remain supported via the byte counter. DIB Create and decoded GDI+ images have a separate 128 MiB pixel budget including 24bpp DIB row padding, enforced before OpenATL bitmap allocation or LockBits; this does not bound GDI+'s internal parser allocations: it accepts S_FALSE at EOF or a short S_OK only when Stat plus a non-moving Seek(CUR, 0) prove the caller stream reached EOF. Without EOF evidence short S_OK fails STG_E_READFAULT; read failures propagate their original HRESULT. Failed reads may advance the caller's position but never install a partial HBITMAP. For saving, an OS-created temporary HGLOBAL IStream holds the completed PNG/BMP encoding. OpenATL copies it in 8 KiB chunks to the caller's stream, rejecting short S_OK writes and propagating failing HRESULTs; partial caller-stream output can remain on failure, but there is no claim of transactional rollback. The image codec consumer explicitly links `gdiplus`; `ole32` is supplied by `OpenATL::ATL`. GDI+ is not a transitive `OpenATL::ATL` or WTL dependency and does not initialize a COM apartment. A failed decode never replaces an existing bitmap; invalid save formats are rejected before writing.

The WTL10 build-local adapter preserves Win32 dialogs, in-memory dialog resources,
MRU menus with optional HKCU persistence, file finding and message loops.
Registry persistence is provided by source-owned Win32 `ATL::CRegKey`, not
COM registration. The adapter removes the separate `CRegProperty`
serializer, legacy Internet Explorer color lookup and COM print callbacks.
The basic WTL UI path does not silently activate COM. Windowed ActiveX
client hosting exists in `atlhost.h`; windowless/control-server coverage
remains a compatibility gap.

Win32 UI can use WTL windowing, dialogs, message maps and common controls
without COM initialization. Registry writes occur only when explicitly
using application settings or MRU persistence; they require `advapi32` at
link time, not a COM Server or COM apartment. `CPath`, `CAtlFile`,
`CString` and native Win32 synchronization live in `include/` and do not
initialize COM. All consumers use the same `OpenATL::ATL` target and its
system link dependencies.

The implemented ATL surface includes `_AtlBaseModule`/`_AtlWinModule`,
`atlcomcli.h` COM Client RAII, `atlreg.h` Win32 registry operations,
`atlcom.h` nonaggregated COM objects/maps, `openatl/atl_module.h` and
`openatl/com_server.h`/`com_registration.h` class factories and native
COM Server registration. Source conformance and native tests must protect
**both** Client and Server APIs. The source policy must not forbid future
standard ATL modules, object maps or registration by name.

**Missing compatibility features, not exclusions:** standard
`CAtlDllModuleT`, automatic/chained object maps, aggregation and tear-offs,
RGS, complete EXE-server registration, type libraries, Automation,
SAFEARRAY wrappers and full ActiveX/OLE controls. Implement these against
original ATL consumers and qualify native behavior. The existing server
supports explicit nonaggregated factories and CLSID/ProgID registration.
See [compatibility matrix](ATL_COMPATIBILITY.md), [server](COM_SERVER.md)
and [module protocol](ATL_MODULE.md).

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
- Project-specific glue/tests use Apache-2.0; RATL/sample references have
  independent provenance. RGS is now an ATL compatibility target, not a
  prohibited implementation. Any new implementation must preserve source
  provenance and native semantics.
- Both llvm-mingw 20261020 and genuine MSVC 2022 build and test Windows
  x64+ARM64; CMake manages WTL on all toolchains, including Linux/macOS
  Windows cross-builds. Each build tree owns its adapted headers.
  No Microsoft ATL include-path fallback.

The W^X thunk and test-only fault injection live in the checked-in
`atlwin.h`; no external checkout is patched. Actual CFG/CET
mitigations, callback teardown/unload races, interactive SDI and
production distribution are tracked separately.

See [toolchain matrix](TOOLCHAIN_MATRIX.md).
