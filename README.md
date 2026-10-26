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
- `OpenATL::WTL`: official WTL10, depending only on `OpenATL::ATL`;
  **does not transitively link COM libraries**.
- `OpenATL::COMClient`: **opt-in** ATL client helpers, depending on
  `OpenATL::ATL`, Windows `ole32`, `oleaut32` and `uuid`.

These are **permanently source-tree CMake INTERFACE targets**. OpenATL
will not provide an Installed SDK, a package configuration, exported
targets, or a `cmake --install` workflow. Consumers integrate the source
repository directly via `add_subdirectory()` or `FetchContent`.
OpenATL always builds the headers checked into this repository's
`include/`; no external ATL header root or fallback is accepted:

```sh
cmake -S . -B build \
  -DOPENATL_WTL_INCLUDE_DIR=/path/to/official-WTL10/Include
```

On Linux/macOS, add the checked-in llvm-mingw toolchain file and its
`OPENATL_LLVM_MINGW_BIN` and `OPENATL_TARGET_ARCH` settings. Only WTL10,
its demo, and the toolchain are fetched as pinned test dependencies;
`OPENATL_ATL_INCLUDE_DIR` is explicitly rejected.

GUI-only applications link `OpenATL::WTL`. Applications that actually
need COM also link `OpenATL::COMClient` and manage their own
`CoInitializeEx` / `CoUninitialize` lifecycle. A basic WTL window must
not need to initialize COM.

## Dependencies and licensing

New OpenATL utilities, tests and CI glue: **Apache-2.0**; the checked-in
clean-room ATL subset derived from
[kkhaike/openatl](https://github.com/kkhaike/openatl):
**MIT** (original revision `7b587f3e10f3f8d93a01522033de80007368bad5`);
[official WTL10](https://github.com/Win32-WTL/WTL) version
`10.0.10320`: **MS-PL**, pinned ZIP SHA256
`13de5cd7ed41454b8f287e9bcc4525234c6ac8c85905e4d4fe1a0776709fd78f`.
There is one ATL implementation: the files owned under `include/`.
`qigao/tcc` separately owns TinyCC/C11/compiler portability.

The in-tree `atlwin.h` includes W^X thunk hardening and test-only
fault injection. No external OpenATL checkout or runtime patch is used.
CFG/CET, concurrency and installability remain unqualified.

## Required CI

- **llvm-mingw** on Windows x64, real Windows ARM64, plus Linux/macOS
  Windows x64+ARM64 cross compilation and real WTL10 SDI resource link.
- **MSVC** x64 native and HostX64→ARM64 build plus real Windows ARM64,
  compiling the same pinned *clean-room* OpenATL (never Microsoft's ATL).
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
