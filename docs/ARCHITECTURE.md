# OpenWTL architecture

OpenWTL combines the WTL UI frontend with the source-owned OpenATL backend.
The backend implements both **COM Client and COM Server**, ATL-compatible
utility classes and Win32 windowing. The existing
`openatl/` explicit server API and `atlcom.h` compatibility templates are
foundation code, not an out-of-scope extension. See
[canonical project scope](PROJECT_SCOPE.md). Current integration:

| Layer | Source / dependencies |
| --- | --- |
| Public UI target | `OpenWTL::WTL`, WTL frontend in `wtl/Include/` |
| Private ATL/COM backend | `openatl_atl`, headers in `alt/include/` |
| Platform | Windows UI, registry and COM system libraries |

The sole public CMake target is `OpenWTL::WTL`. `openatl_atl` is internal;
the old public target is removed. The WTL headers, AppWizard, license and
11 original sample trees reside under `wtl/`. No vcpkg WTL installation,
build-local adapter or alternate provider is used. Build integration uses the source tree; no installed SDK or installer/consumer
project is supplied.
Optional CImage BMP/PNG file and caller-owned `IStream*` codec calls use Windows GDI+ with a per-call startup/shutdown scope and explicit `gdiplus` linkage by the consumer. Stream I/O never initializes a COM apartment or takes ownership of the stream; the caller handles positioning and release. File Load opens its Win32 input handle once and copies at most 64 MiB into an OS-owned HGLOBAL stream before invoking GDI+; the same bytes whose size was checked are decoded, and the file handle is closed before returning. Stream Load snapshots up to 64 MiB with exact HRESULT/byte-count checks before GDI+ decoding. Where supported, Stat and Seek(CUR, 0) also preflight the 64 MiB remaining-body limit before reading; unseekable streams remain supported via the byte counter. DIB Create and decoded GDI+ images have a separate 128 MiB pixel budget including 24bpp DIB row padding, enforced before OpenATL bitmap allocation or LockBits; this does not bound GDI+'s internal parser allocations: it accepts S_FALSE at EOF or a short S_OK only when Stat plus a non-moving Seek(CUR, 0) prove the caller stream reached EOF. Without EOF evidence short S_OK fails STG_E_READFAULT; read failures propagate their original HRESULT. Failed reads may advance the caller's position but never install a partial HBITMAP. For saving, an OS-created temporary HGLOBAL IStream holds the completed PNG/BMP encoding. OpenATL copies it in 8 KiB chunks to the caller's stream, rejecting short S_OK writes and propagating failing HRESULTs; partial caller-stream output can remain on failure, but there is no claim of transactional rollback. The image codec consumer explicitly links `gdiplus`; `ole32` is supplied by `OpenWTL::WTL`. GDI+ is not a transitive `OpenWTL::WTL` or WTL dependency and does not initialize a COM apartment. A failed decode never replaces an existing bitmap; invalid save formats are rejected before writing.

WTL10 retains its original public dialogs, message loops, MRU menus,
CRegProperty, hyperlink settings and COM print callbacks. Compatibility
patches live directly in the repository-owned WTL source, not in a
generated adapter. Native behavior remains separately tested.

Win32 UI can use WTL windowing, dialogs, message maps and common controls
without COM initialization. Registry writes occur only when explicitly
using application settings or MRU persistence; they require `advapi32` at
link time, not a COM Server or COM apartment. `CPath`, `CAtlFile`,
`CString` and native Win32 synchronization live in `alt/include/` and do not
initialize COM. All consumers use the same `OpenWTL::WTL` target and its
system link dependencies.

The implemented ATL surface includes `_AtlBaseModule`/`_AtlWinModule`,
`atlcomcli.h` COM Client RAII, `atlreg.h` Win32 registry operations,
`atlcom.h` nonaggregated COM objects/maps, `openatl/atl_module.h` and
`openatl/com_server.h`/`com_registration.h` class factories and native
COM Server registration. Source conformance and native tests must protect
**both** Client and Server APIs. The source policy must not forbid future
standard ATL modules, object maps or registration by name.

**Missing compatibility features, not exclusions:** expanded object maps,
aggregation and tear-offs, full RGS, complete EXE-server registration,
complete type-library registration and Automation,
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

Image-based `IDispatchImpl` uses the standard 0xffff/0xffff version sentinel
to load its own PE image with REGKIND_NONE. The backend validates LIBID/IID,
retains no static ITypeInfo cache and does not register the TypeLib.
`CAtlBaseModule` uses the image-local PE linker symbol instead of the host
process EXE handle.

## Upstream pins and licenses

- Clean-room MIT ATL source under `alt/include/`, originally derived from
  `kkhaike/openatl@7b587f3e10f3f8d93a01522033de80007368bad5`.
  CI only compiles the checked-in headers and refuses an external ATL root.
- WTL10 MS-PL source resides in `wtl/`, pinned to release 10.0.10320.
  `cmake/OpenATLWTL.cmake` exposes the checked-in headers directly.
  `wtl/SOURCE.md` records the seven compatibility patches and
  `wtl/source-manifest.json` inventories the complete 296-file release.
  All original binary resources and license notices remain in-tree.
- WTL sample `kkhaike/wtltest@63ba6a992cb3d79d56be66cd283f6615c6646b3d`.
- Project-specific glue/tests use Apache-2.0; RATL/sample references have
  independent provenance. RGS is now an ATL compatibility target, not a
  prohibited implementation. Any new implementation must preserve source
  provenance and native semantics.
- Both llvm-mingw 20261020 and genuine MSVC 2022 build and test Windows
  x64+ARM64; CMake uses the same repository WTL source on all toolchains, including
  Linux/macOS Windows cross-builds.
  No Microsoft ATL include-path fallback.

The W^X thunk and test-only fault injection live in the checked-in
`atlwin.h`; no external checkout is patched. Actual CFG/CET
mitigations, callback teardown/unload races, interactive SDI and
production distribution are tracked separately.

See [toolchain matrix](TOOLCHAIN_MATRIX.md).
