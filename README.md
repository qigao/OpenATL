# OpenATL

**OpenATL is a Windows UI library for ATL-style Win32 windowing, official
WTL 10, and optional COM *client* helpers.** Its only supported scope is:

- Win32 GUI: `CWindowImpl`, `CDialogImpl`, message maps, common controls,
  resources and WTL10 frames, without `CoInitializeEx()`.
- In-tree ATL utilities: `CString`, `CAtlFile`, `CPath`, and Win32
  synchronization wrappers; no COM apartment needed.
- COM **client**: `CComPtr`, `CComBSTR`, `CComVariant`, and
  `CoCreateInstance` of existing Windows components after the *application*
  explicitly initializes a COM apartment.

**Not supported:** COM server creation, `IClassFactory` implementations,
`OBJECT_ENTRY_AUTO`, `DllRegisterServer/UnregisterServer`, RGS,
registry-based COM server installation, ActiveX hosting or a production
class-object map. Do not infer these capabilities from unmodified upstream
ATL header declarations. No RATL source, fallback or compatibility layer.

## CMake consumer targets

- `OpenATL::ATL`: clean-room ATL header subset for Win32 UI utilities.
- `wtl`: **third-party dependency managed by `vcpkg.json`** (WTL10
  `10.0.10320#6` overlay). WTL is header-only and has **no OpenATL
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

## Dependencies and licensing

New OpenATL utilities, tests and CI glue: **Apache-2.0**; the checked-in
clean-room ATL subset derived from
[kkhaike/openatl](https://github.com/kkhaike/openatl):
**MIT** (original revision `7b587f3e10f3f8d93a01522033de80007368bad5`);
[official WTL10](https://sourceforge.net/projects/wtl/) version
`10.0.10320` via vcpkg overlay revision `#6`: **MS-PL**; the
`ports/wtl/portfile.cmake` upstream source archive has pinned SHA512
`086a6cf6a49a4318a8c519136ba6019ded7aa7f2c1d85f78c30b21183654537b3428a400a64fcdacba3a7a10a9ef05137b6f2119f59594da300d55f9ebfb1309`.
There is one ATL implementation: the files owned under `include/`.
`qigao/tcc` separately owns TinyCC/C11/compiler portability.

The in-tree `atlwin.h` includes W^X thunk hardening and test-only
fault injection. No external OpenATL checkout or runtime patch is used.
CFG/CET and concurrency remain unqualified. OpenATL installation is
permanently out of scope; installing the external WTL dependency with
vcpkg does not install OpenATL.

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
