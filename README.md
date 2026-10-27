# OpenATL

**OpenATL is a Windows UI library for ATL-style Win32 windowing, official
WTL 10, and optional COM *client* helpers.** Its only supported scope is:

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
  have native Windows tests; no registration or COM Server is added.

**Not supported or present:** COM server creation, `IClassFactory`
implementations, `OBJECT_ENTRY_AUTO`, `DllRegisterServer/UnregisterServer`,
RGS, COM registration and ActiveX hosting, and COM
class-object maps. A separate source-owned Win32 CRegKey handles ordinary
application settings; it is not a registrar. The former server-only `atlcom.h`, `CComModule`,
`CAtlModuleT`, `CAtlComModule`, object maps and no-op
`RegisterServer`/`AtlAxWinInit` stubs have been **deleted**, not replaced
with compatibility shims. The real `_AtlBaseModule` and `_AtlWinModule`
remain for Windows UI; `atlcomcli.h` remains only for the COM **client**.
No RATL provider, fallback or alternate ATL implementation.

## CMake consumer targets

- `OpenATL::ATL`: clean-room ATL header subset for Win32 UI utilities.
- `wtl`: **third-party dependency managed by `vcpkg.json`** (WTL10
  `10.0.10320#10` overlay). WTL is header-only and has **no OpenATL
  namespaced target**; no dependency on Visual Studio's Microsoft ATL.
- `OpenATL::COMClient`: **opt-in** ATL client helpers, depending on
  `OpenATL::ATL`, Windows `ole32`, `oleaut32` and `uuid`.

The two `OpenATL::` targets are **permanently source-tree CMake INTERFACE
targets**. OpenATL
will not provide an Installed SDK, a package configuration, exported
targets, or a `cmake --install` workflow. Consumers integrate the source
repository directly via `add_subdirectory()` or `FetchContent`.
OpenATL always builds the headers checked into this repository's
`include/`; no external ATL header root or fallback is accepted.

Install WTL from the version-pinned vcpkg manifest in the repository root.
The `ports/wtl` overlay retains the official vcpkg port's source checksum
and maintenance patches, **removing only its dependency on the Microsoft
ATL stub**. For example, on an MSVC Windows x64 host:

```sh
vcpkg install --triplet x64-windows-static \
  --x-install-root=vcpkg_installed/x64-windows-static
cmake -S . -B build -DOPENATL_BUILD_QUALIFICATION=ON \
  -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build build
ctest --test-dir build --output-on-failure
```

Windows llvm-mingw, Linux and macOS cross-host builds use the custom
`cmake/vcpkg-triplets/{x64,arm64}-openatl-mingw.cmake` triplets.
Their compilers use `cmake/toolchains/llvm-mingw.cmake`, and CI
installs WTL through vcpkg before configuring OpenATL. The CI uses
a separate manifest install root per triplet (at
`vcpkg_installed/<triplet>/<triplet>/include`) so installing ARM64 does
not remove previously installed x64 headers. Manually supplied
`OPENATL_ATL_INCLUDE_DIR` and `OPENATL_WTL_INCLUDE_DIR` are rejected.

A GUI application links **`OpenATL::ATL`** and obtains WTL's
`atlapp.h`, `atlctrls.h`, etc. from the vcpkg include directory.
When using a vcpkg toolchain, consumers can locate the header directory
with `find_path(WTL_INCLUDE_DIR NAMES atlapp.h REQUIRED)` and add it to
their own GUI target; there is deliberately no `OpenATL::WTL`.
Consumers needing COM additionally link `OpenATL::COMClient` and manage
their own `CoInitializeEx` / `CoUninitialize` lifecycle.
A basic WTL window must not need to initialize COM.

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
  invoking `CImage::Load/Save` must link Windows `gdiplus` and `ole32` explicitly;
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
  callers link both `gdiplus` and `ole32` explicitly; neither is a
  transitive `OpenATL::ATL`/WTL dependency.
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
third-party source provenance remain unchanged. No COM server, registry
registration, Installed SDK or `scripts/` directory is added.

## Dependencies and licensing

New OpenATL utilities, tests and CI glue: **Apache-2.0**; the checked-in
clean-room ATL subset derived from
[kkhaike/openatl](https://github.com/kkhaike/openatl):
**MIT** (original revision `7b587f3e10f3f8d93a01522033de80007368bad5`);
[official WTL10](https://sourceforge.net/projects/wtl/) version
`10.0.10320` via vcpkg overlay revision `#10`: **MS-PL**; the
`ports/wtl/portfile.cmake` upstream source archive has pinned SHA512
`086a6cf6a49a4318a8c519136ba6019ded7aa7f2c1d85f78c30b21183654537b3428a400a64fcdacba3a7a10a9ef05137b6f2119f59594da300d55f9ebfb1309`.
There is one ATL implementation: the files owned under `include/`.
`qigao/tcc` separately owns TinyCC/C11/compiler portability.

The in-tree `atlwin.h` includes W^X thunk hardening and test-only
fault injection. No external OpenATL checkout or runtime patch is used.
CFG/CET and concurrency remain unqualified. OpenATL installation is
permanently out of scope; installing the external WTL dependency with
vcpkg does not install OpenATL.

## CI structure

The pinned WTL UI overlay retains MRU menu helpers, CFindFile and ordinary
HKCU registry MRU persistence through source-owned CRegKey, with native menu,
capacity, dedup, reorder and persistence tests. It still removes the separate
CRegProperty serializer, Internet Explorer registry hyperlink colors and
COM-based extended print callbacks. No COM Server API or registration shim is reintroduced. Win32 CRegKey
is confined to include/atlreg.h; its native HKCU tests cover ownership,
values, errors and access masks without COM initialization.

The former 14-file `scripts/` directory has been deleted. GitHub Actions now installs pinned WTL through `vcpkg.json`, invokes CMake and CTest directly, and validates Windows x64/ARM64 PE files. Real ARM64 SHA-256 and runtime checks are defined as acceptance tests in `tests/wtl10/native_arm64.py`. No standalone build wrapper or Installed SDK is provided.

## Required CI

- **llvm-mingw** on Windows x64, real Windows ARM64, plus Linux/macOS
  Windows x64+ARM64 cross compilation and real WTL10 SDI resource link.
- **MSVC** x64 native and HostX64→ARM64 build plus real Windows ARM64,
  compiling the same pinned *clean-room* OpenATL (never Microsoft's ATL).
- **WTL dependency provenance**: required `vcpkg.json` manifest,
  local overlay `ports/wtl` (no Microsoft `atl` dependency), native and
  cross-target installed header checks, and no manual ZIP fallback.
- Separate native assertions: UI and the repository-owned file/string/sync
  utilities run **without COM initialization**; the standalone Unicode
  `CPath` test exercises actual Windows file creation and path handling.
  The optional COM Client starts its own apartment and activates a built-in
  Windows component. All native suites run five repetitions on Windows
  x64/ARM64, with SHA256/PE checks for ARM64 handoff.

See [architecture](docs/ARCHITECTURE.md),
[acceptance](docs/ACCEPTANCE.md), and
[toolchain matrix](docs/TOOLCHAIN_MATRIX.md).

**Draft / qualification only.** Full GUI CFG/CET, failure cleanup, DSO
unloading and interactive SDI still need explicit acceptance. No WTL8
fallback, no Salts production ABI or compiler switch. **Installed SDK is
permanently out of scope**; there is no future installation gate.
