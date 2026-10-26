# OpenATL

**OpenATL is a Windows UI library for ATL-style Win32 windowing, official
WTL 10, and optional COM *client* helpers.** Its only supported scope is:

- Win32 GUI: `CWindowImpl`, `CDialogImpl`, message maps, common controls,
  resources and WTL10 frames, without `CoInitializeEx()`.
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

These are currently **source-tree interface targets, not an installable
SDK**. External source headers must be supplied explicitly:

```sh
cmake -S . -B build \
  -DOPENATL_ATL_INCLUDE_DIR=/path/to/clean-room-openatl/include \
  -DOPENATL_WTL_INCLUDE_DIR=/path/to/official-WTL10/Include
```

GUI-only applications link `OpenATL::WTL`. Applications that actually
need COM also link `OpenATL::COMClient` and manage their own
`CoInitializeEx` / `CoUninitialize` lifecycle. A basic WTL window must
not need to initialize COM.

## Dependencies and licensing

OpenATL project code: **Apache-2.0**; separately fetched external
[OpenATL clean-room headers](https://github.com/kkhaike/openatl):
**MIT** (pinned `7b587f3e10f3f8d93a01522033de80007368bad5`);
[official WTL10](https://github.com/Win32-WTL/WTL) version
`10.0.10320`: **MS-PL**, pinned ZIP SHA256
`13de5cd7ed41454b8f287e9bcc4525234c6ac8c85905e4d4fe1a0776709fd78f`.
No external source is vendored as the production provider.
`qigao/tcc` separately owns TinyCC/C11/compiler portability.

Only the exact-pinned OpenATL **WTL W^X thunk patch** is applied to a
temporary CI checkout. It is not a production-ready CFG/CET,
concurrency-safe or installable implementation.

## Required CI

- **llvm-mingw** on Windows x64, real Windows ARM64, plus Linux/macOS
  Windows x64+ARM64 cross compilation and real WTL10 SDI resource link.
- **MSVC** x64 native and HostX64→ARM64 build plus real Windows ARM64,
  compiling the same pinned *clean-room* OpenATL (never Microsoft's ATL).
- Separate native assertions: WTL UI runs **with COM uninitialized**;
  COM Client starts its own apartment and activates a built-in system COM
  component. Both must pass five repetitions and SHA256/PE checks.

See [architecture](docs/ARCHITECTURE.md),
[acceptance](docs/ACCEPTANCE.md), and
[toolchain matrix](docs/TOOLCHAIN_MATRIX.md).

**Draft / qualification only.** Full GUI CFG/CET, failure cleanup, DSO
unloading, interactive SDI and an installed SDK still need explicit
acceptance. No WTL8 fallback, no Salts production ABI or compiler switch.
