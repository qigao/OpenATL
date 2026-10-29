# OpenATL

**OpenATL is a source-compatible implementation of the Windows Active
Template Library (ATL), including COM Client, COM Server, and Win32/WTL 10.**
The present implementation is a growing, tested subset of the complete ATL
API—not an intentionally client-only library. The earlier exclusions of COM
Server functionality are void. See [project charter](docs/PROJECT_SCOPE.md)
and [ATL compatibility matrix](docs/ATL_COMPATIBILITY.md).

The implemented foundation includes:

- Win32 GUI: `CWindowImpl`, `CDialogImpl`, message maps, common controls,
  resources and WTL10 frames, without `CoInitializeEx()`.
- In-tree ATL utilities: `CString`, `CAtlFile`, `CPath`, Win32
  synchronization, plus `atlmem.h` (`IAtlMemMgr`, `CWin32Heap`),
  `atltime.h` (`CFileTime`, `CFileTimeSpan`),
  `atlcoll.h` (`CAtlArray`), `atlfile.h` (`CAtlFileMappingBase`,
  `CAtlFileMapping<T>`) and `atlimage.h` (Win32 `CImage` subset);
  no COM apartment needed.
- COM **client**: `CComPtr`, `CComBSTR`, `CComVariant`, and
  `CoCreateInstance` of existing Windows components after the *application*
  explicitly initializes a COM apartment. BSTR self-assignment and
  self-append, embedded-NUL comparisons, length overflow rejection,
  VARIANT copy/Detach ownership and COM identity-query failure handling
  have native Windows tests. COM Client and COM Server are both required,
  while individual applications choose which API family they use.

The `OpenATL::COM` server API supplies explicit class/interface tables,
`IClassFactory`, reference and module-lock accounting, and real CLSID/ProgID
registration. Standard `ATL::CAtlDllModuleT<T>` now forwards actual DLL
exports to this one factory/lock engine through an explicit bound class
table; applications still export their own DLL entry points. This is an
implemented server milestone. Standard `OBJECT_ENTRY_AUTO(clsid, class)`
now discovers static COM class entries automatically, composes the **same**
class-factory engine and supports native STA/MTA DLL activation without
manually calling `BindServer`. A class-defined
`static HRESULT WINAPI UpdateRegistry(BOOL)` now drives real DLL registration
in CLSID order, with inverse callback rollback on failure. Missing class
registration hooks fail `E_NOTIMPL`; `DECLARE_NO_REGISTRY()` is an explicit
opt-out. `DECLARE_REGISTRY_RESOURCEID(id)` now loads a **real Win32
REGISTRY resource** from an explicit image inside a scoped, caller-owned
registration context, then uses the existing bounded RGS parser and
owner-verified executor. The framework neither chooses HKCR nor infers
installer metadata, and full ATL TypeLib/COM map compatibility remains open.
See [server contracts](docs/COM_SERVER.md) and
[DLL module lifetime](docs/ATL_MODULE.md).

The optional `atlcom.h` implements familiar nonaggregated ATL object templates
and COM maps: `CComObjectRootEx`, `CComObject`, `CComCoClass`, `CComCreator`,
`BEGIN_COM_MAP`, `COM_INTERFACE_ENTRY` and explicit-IID/branch variants.
See the [ATL compatibility matrix](docs/ATL_COMPATIBILITY.md) for supported
semantics and limits. `CAtlDllModuleT` is implemented for explicit class
tables; static `OBJECT_ENTRY_AUTO` factory discovery is supported.
Class-directed `UpdateRegistry` and a bounded original `REGISTRY` resource
path are implemented; arbitrary RGS variants, TypeLib registration,
expanded COM maps and aggregation remain compatibility work. `CComModule`
is currently absent and will be evaluated against actual ATL consumers. Windowed ActiveX
containment and typed Automation event sinks are described in
[the hosting contract](docs/ACTIVEX_HOST.md); resource-based ActiveX dialogs
and windowless hosting remain pending. MFC is not a dependency.
There is one clean-room ATL implementation, with no Microsoft ATL fallback.

## CMake integration

Downstream applications need only the OpenATL source checkout and one target:

```cmake
add_subdirectory(external/OpenATL)
add_executable(my_app WIN32 main.cpp)
target_link_libraries(my_app PRIVATE OpenATL::ATL)
```

`FetchContent` can bring in OpenATL instead of `add_subdirectory`; pin the
OpenATL commit in the consuming project. `OpenATL::ATL` supplies ATL and WTL
include paths, C++17/Unicode definitions, and common Windows UI/COM/registry
link dependencies. Consumers can include `<atlbase.h>`, `<atlcom.h>`,
`<atlapp.h>`, `<atlframe.h>`, etc. directly. No overlay ports, WTL install,
manual header lookup, `OpenATL::WTL` target or Microsoft ATL installation is
needed. Products using optional image codecs still link `gdiplus` explicitly.

During the first CMake configuration, OpenATL fetches the official WTL
`10.0.10320` release with a pinned SHA512, copies its headers into the build
directory and applies the source-owned adapter. The upstream tree and consumer
files are never patched. A fresh build needs access to SourceForge; populated
builds reuse their FetchContent sources. Controlled offline builds can supply
the exact unpacked release through CMake's standard
`FETCHCONTENT_SOURCE_DIR_OPENATL_WTL_SOURCE` setting. Missing/invalid content
fails configuration. vcpkg remains available for the application's other
dependencies; OpenATL does not change the application's vcpkg configuration.
OpenATL itself remains a source-tree CMake INTERFACE target with no
install/export/package-config workflow.

For repository qualification, use the versioned presets. From an x64 VS
developer shell:

```sh
cmake --preset msvc-x64
cmake --build --preset msvc-x64
ctest --preset msvc-x64
```

For llvm-mingw, set `OPENATL_LLVM_MINGW_BIN` to the compiler `bin` directory,
then use `llvm-x86_64` or `llvm-aarch64`. MSVC ARM64 uses `msvc-arm64` from an
ARM64-targeting VS developer shell. The optional full SDI sample is selected
with `OPENATL_WTL_DEMO_DIR`. CI builds the pinned sample without source edits.
Generic hosted LLVM and MSVC tests cannot guarantee hardware HSP;
a separate strict hardware CET workflow qualifies that property on a vetted
native Windows x64 runner (#15), without treating its absence as success.
`OPENATL_ATL_INCLUDE_DIR` and
`OPENATL_WTL_INCLUDE_DIR` overrides are obsolete and rejected.

## Unicode text

`OpenATL::ATL` provides strict, counted UTF-8/UTF-16/ACP conversions through
`openatl/text_encoding.h`, plus checked `AssignUtf8`, `AssignAnsi` and `ToUtf8`
methods on `CString`/`CComBSTR`. Existing narrow constructors and `A2W`/`W2A`
keep ACP semantics, with invalid input rejected consistently. Owned conversion
outputs preserve embedded NULs and retain their old value on failure.

For explicit Unicode 17 NFC, full case folding and whitespace processing,
enable `OPENATL_USE_SALTS_UNICODE=ON` with a matching installed `SALTS_ROOT`
and include `openatl/unicode.h`. This remains the same `OpenATL::ATL` target;
the default Windows conversion path has no Salts dependency. See the
[encoding, error and SDK contracts](docs/UNICODE.md).

## Expanded ATL API surface

The new headers implement **ATL-compatible interfaces directly in OpenATL**.
Their method names and public conventions are informed by ATL and
[katahiromz/RATL](https://github.com/katahiromz/RATL), but each implementation
is part of this repository (not a second ATL provider).

- `atlmem.h`: Win32 heap allocation, reallocation, free and size lookup,
  with explicit non-owning heap lifetime.
- `atltime.h`: 100-nanosecond `FILETIME` values, signed spans, comparisons,
  arithmetic and optional overflow-checked addition.
- `atlcoll.h`: `CAtlArray` and `CAtlList`, with safe nontrivial
  object lifetime and stable node-specific opaque `POSITION` handles.
  The list supports bidirectional traversal, head/tail insertion and
  removal, search, and adjacent/nonadjacent node exchanges; foreign
  or removed positions are validated before dereference. Unlike the
  incomplete RATL list allocator, every node has one clear owner,
  and throwing element copies leave the list unchanged.
  The optional ATL block-pool constructor is deliberately not offered:
  node allocation uses owned C++ `new`/`delete`, not fake pool behavior.
- `atlfile.h`: file-backed and named shared-memory mapping, including
  unaligned Win32 view offsets, explicit close/unmap, move ownership,
  independent handle/view duplication with `CopyFrom` and bounded mapping
  sizes. `MapFile` deliberately rejects extending the mapped file: callers
  explicitly size the underlying file through `CAtlFile` first.
- `atlimage.h`: a bounded Win32 GDI `CImage` subset with owned
  `HBITMAP`, Create/Attach/Detach/Destroy, top-down and bottom-up DIB
  pixel access, GetDC/ReleaseDC and BitBlt/StretchBlt-based Draw. `Load`
  and `Save` support BMP and PNG through the Windows GDI+ runtime,
  with full BGRA pixel copying and PNG alpha preservation. Codec state
  is scoped to each call and always shut down before returning. Consumers
  invoking `CImage::Load/Save` must link Windows `gdiplus` explicitly; `ole32` is supplied by `OpenATL::ATL`;
  basic GDI and WTL UI targets do **not** link `gdiplus` transitively.
  A caller-owned `IStream*` can also be used for BMP/PNG `Load` and
  `Save` using an explicit `ImageFormatBMP` or `ImageFormatPNG` GUID.
  OpenATL borrows (never closes or retains) the stream; callers position and
  release their streams themselves. Stream saving encodes into an OS-owned
  temporary memory stream before writing in 8 KiB chunks to the caller's
  `IStream`. A short successful write returns `STG_E_WRITEFAULT`, and a
  failed write returns the destination's original HRESULT. A failed write
  may leave a partial output in the **caller** stream; arbitrary IStreams
  do not provide atomic rollback. Stream `Load` reads a bounded (64 MiB)
  snapshot into an OS-owned memory stream before decoding. A short `S_OK`
  read requires an exact EOF proof through `IStream::Stat` and a nonmoving
  `Seek(STREAM_SEEK_CUR, 0)`; otherwise it returns `STG_E_READFAULT`.
  Failed source read HRESULTs propagate unchanged. This accommodates
  Windows HGLOBAL streams without accepting arbitrary silent truncation.
  File `Load` opens the source once, checks the 64 MiB encoded-file size,
  and creates a bounded immutable HGLOBAL snapshot **before** GDI+ decoding.
  This avoids a separate metadata-check / decoder-reopen TOCTOU gap and
  guarantees the source file handle is closed before returning. If `Stat` and a nonmoving `Seek(CUR, 0)` are supported, a known
  remaining IStream range above 64 MiB is rejected before the first read;
  otherwise the running byte counter enforces the same cap.
  All retained `CImage` bitmaps are subject to the 128 MiB
  aligned row-byte budget: `Create`, decoded DIBs, and external
  `HBITMAP` handles supplied to `Attach` (which remain caller-owned
  if the attachment is rejected). Decoder dimensions are checked before
  allocating the destination bitmap or locking GDI+ pixels. This is
  not a limit on GDI+'s internal parsing allocations. Callers must account for the
  source stream's advanced position on failure.
  The image and stream ownership remain unchanged. Image stream codec
  callers link `gdiplus` explicitly; `ole32` is supplied by `OpenATL::ATL`.
  GDI+ remains outside the transitive `OpenATL::ATL`/WTL dependencies.
  File and stream paths share validated decoder, pixel copy, encoder
  selection and cleanup.
  GIF/JPEG/TIFF codecs, alpha compositing, GDI+ metadata, and the full
  Microsoft ATL CImage API are **not implemented or qualified**.

The public API is a subset of ATL; unsupported functions are not advertised
as implemented. These utilities are CI-qualified through
`tests/atl_conformance/atl_extensions.cpp` on Windows x64, genuine
Windows ARM64 and Linux/macOS Windows PE cross-builds.

API declarations and naming compatibility do not require copying Microsoft
ATL or RATL implementation code. The existing OpenATL file notices and
third-party source provenance remain unchanged. These ATL utilities do not
depend on the COM Server/registration API. No Installed SDK or `scripts/`
directory is currently included.

## Dependencies and licensing

New OpenATL utilities, tests and CI glue: **Apache-2.0**; the checked-in
clean-room ATL subset derived from
[kkhaike/openatl](https://github.com/kkhaike/openatl):
**MIT** (original revision `7b587f3e10f3f8d93a01522033de80007368bad5`);
[official WTL10](https://sourceforge.net/projects/wtl/) version
`10.0.10320`: **MS-PL**; `cmake/OpenATLWTL.cmake` pins its source archive SHA512
`086a6cf6a49a4318a8c519136ba6019ded7aa7f2c1d85f78c30b21183654537b3428a400a64fcdacba3a7a10a9ef05137b6f2119f59594da300d55f9ebfb1309`.
There is one ATL implementation: the files owned under `include/`.
`qigao/tcc` separately owns TinyCC/C11/compiler portability.

The in-tree `atlwin.h` includes W^X thunk hardening and test-only
fault injection. No external OpenATL checkout or runtime patch is used.
CFG/CET and concurrency qualification is scoped to the native test fixtures;
downstream products must validate their own lifetime and mitigation contracts.
The current source-tree build is qualified; installed/exported integration
may be developed with independent acceptance under issue #4. Managed WTL
sources and adapted headers remain build dependencies.

## CI structure

The pinned WTL UI adapter retains MRU menu helpers, CFindFile and ordinary
HKCU registry MRU persistence through source-owned CRegKey, with native menu,
capacity, dedup, reorder and persistence tests. It still removes the separate
CRegProperty serializer, Internet Explorer registry hyperlink colors and
COM-based extended print callbacks. WTL itself acquires no server API or
registration shim. Win32 CRegKey
is confined to `include/atlreg.h`; native HKCU tests cover ownership,
malformed strings, values, errors, access masks and cleanup without COM
initialization. Applications using its registry methods explicitly link
Windows `advapi32`, supplied by `OpenATL::ATL`.

The former 14-file `scripts/` directory has been deleted. GitHub Actions now obtains pinned WTL through CMake FetchContent and invokes CMake/CTest presets, and validates Windows x64/ARM64 PE files. Real ARM64 SHA-256 and runtime checks are defined as acceptance tests in `tests/wtl10/native_arm64.py`. No standalone build wrapper or Installed SDK is provided.

## Required CI

- **llvm-mingw** on Windows x64, real Windows ARM64, plus Linux/macOS
  Windows x64+ARM64 cross compilation and real WTL10 SDI resource link.
- **MSVC** x64 native and HostX64→ARM64 build plus real Windows ARM64,
  compiling the same pinned *clean-room* OpenATL (never Microsoft's ATL).
- **WTL dependency provenance**: SHA512-pinned release, build-local adapter,
  transitive includes/linkage and unmodified SDI, on native and cross hosts.
- Separate native assertions: UI and the repository-owned file/string/sync
  utilities run **without COM initialization**; the standalone Unicode
  `CPath` test exercises actual Windows file creation and path handling.
  The optional COM Client starts its own apartment and activates a built-in
  Windows component. All native suites run five repetitions on Windows
  x64/ARM64, with SHA256/PE checks for ARM64 handoff.

See [architecture](docs/ARCHITECTURE.md),
[acceptance](docs/ACCEPTANCE.md), and
[toolchain matrix](docs/TOOLCHAIN_MATRIX.md).

The optional COM server has separate core/registry and STA/MTA DLL activation
tests. Each DLL test registers its fixture under the current user's Classes
root, verifies real COM activation, unregisters it and checks DLL retirement.
Other entries are not overwritten or recursively deleted.

**Source-tree qualification is bounded by the documented APIs and tests.**
Downstream product acceptance remains explicit. No WTL8
fallback, no Salts production ABI or compiler switch. An installed SDK is
not currently implemented; packaging is an open engineering decision (#4),
not prohibited by the ATL compatibility goal.
