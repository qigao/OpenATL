# OpenATL

**OpenATL** is a Windows-first, C++17 integration and compatibility project for
**WTL 10.0.10320** (Windows Template Library) and a pinned **OpenATL** subset
of the Active Template Library. Its conformance suite is built using
**llvm-mingw Clang++ and Microsoft Visual C++ (MSVC)**. llvm-mingw cross-builds
Windows x64/ARM64 PE on Windows, Linux and macOS; MSVC builds them using
Microsoft's Visual Studio/Windows SDK tools on genuine Windows hosts.

**Status: experimental / qualification only.** This repository is not a full
replacement for Microsoft ATL, an ActiveX implementation, or an installable
production COM server. A passing cross-compilation does not replace real
Windows runtime tests.

## Repository boundary

- **OpenATL owns:** Windows WTL/ATL integration, CMake `OpenATL::ATL` and
  `OpenATL::WTL` *interface targets*, GUI lifecycle/resource acceptance,
  COM identity/factory/object-map/isolated registry conformance, and Windows
  x64 + **real native ARM64** test admission.
- **[qigao/tcc](https://github.com/qigao/tcc) owns:** TinyCC,
  C11/atomic/thread/assembler/compiler portability and TinyCC consumer tests.
  OpenATL's default build does not require TinyCC or a C++/COM ABI bridge from it.
- **[kkhaike/OpenATL](https://github.com/kkhaike/openatl)** is the external
  MIT-licensed ATL header implementation. It is fetched at a fixed commit;
  the temporary W^X and COM acceptance patches are *test-only*, not shipped.
- **[Win32-WTL/WTL](https://github.com/Win32-WTL/WTL)** supplies the external
  Microsoft Public License (MS-PL) WTL 10.0.10320 headers/resources.
- **[katahiromz/RATL](https://github.com/katahiromz/RATL)** is used only for
  independently compiled Windows x64 reference executables under its own
  LGPL/GPL file notices. Never combine RATL and OpenATL C++ headers or
  ownership. No RATL Windows ARM64 support is claimed.

## Build consumer targets

Provide Windows-targeting C++17 compiler and the two *separately obtained*
include trees. Do **not** vendor Microsoft ATL or RATL in OpenATL:

```sh
cmake -S . -B build \
  -DOPENATL_ATL_INCLUDE_DIR=/path/to/openatl/include \
  -DOPENATL_WTL_INCLUDE_DIR=/path/to/WTL/Include
```

Link against `OpenATL::ATL` (OpenATL headers and COM fundamentals) or
`OpenATL::WTL` (the WTL10 layer, which depends on `OpenATL::ATL`).
These are **header-only build-configuration targets**, not binaries or
complete Microsoft ATL API compatibility assurances.

## Qualification

The `feature/wtl10-openatl-foundation` branch retains separate pinned
test-only patches and Win32 fixtures previously exercised in the TinyCC
POC. OpenATL's own workflows must pass on the **exact new commit** before
the branch is considered qualified:

- **llvm-mingw**: Windows x64 native and real Windows ARM64 GUI/COM runtime
  5×; Linux/macOS independently cross-build x64+ARM64 PE and link official
  WTL10 SDI with real `.rc` resources. RATL x64 differential tests remain
  in their separate, license-scoped reference executables.
- **MSVC**: Windows x64 genuine native CTest 6/6 repeated 5×, and an
  independent VS2022 `cl.exe` HostX64→ARM64 cross-build. A real Windows
  ARM64 runner must execute MSVC-produced GUI/COM PE 5× after verifying
  machine ID and SHA256. Both MSVC targets also link full WTL10 SDI
  with real Windows SDK `rc.exe`; interactive SDI is not qualified.
- Both compilers must use the **same pinned MIT OpenATL provider**,
  never Microsoft's ATL fallback or RATL mixed headers. No test-only
  patched third-party source is a production SDK.

See [architecture and licensing](docs/ARCHITECTURE.md) and
[conformance boundaries](docs/ACCEPTANCE.md) and
[toolchain matrix](docs/TOOLCHAIN_MATRIX.md). Legacy WTL8/WTL4MinGW,
MSVC ATL fallback, and mixed-provider header builds are not supported.

## Development status

No source from external dependencies is redistributed by default.
Test patches modify **temporary pinned checkout** headers during CI only.
OpenATL's Apache-2.0 license applies to its own independently authored
project source; it does not relicense WTL, OpenATL, or RATL.
