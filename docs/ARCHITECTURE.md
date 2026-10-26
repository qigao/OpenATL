# OpenATL UI + optional COM client architecture

Two independent functional paths share repository-owned clean-room ATL headers:

```
Win32 GUI application
  ├─ OpenATL::ATL ──> in-repository include/ ──> Win32 windows / messages
  └─ wtl (vcpkg.json) ──> official WTL10 header-only dependency
       (NO OpenATL::WTL target; NO COM apartment initialization)

Optional system COM consumer
  └─ OpenATL::COMClient ──> OpenATL::ATL + Windows ole32 / oleaut32 / uuid
       (caller owns CoInitializeEx / CoUninitialize; CLIENT ONLY)
```

`OpenATL::ATL` is a header-only build configuration
targets. `OpenATL::COMClient` is an explicitly selected client dependency.
They exist only within a source-tree CMake build. OpenATL **never** defines
`install()`, `export()`, a package config, or an Installed SDK. Consumers
build from the repository checkout; a post-install consumption path is
permanently excluded.
Win32 UI can use WTL windowing, dialogs, message maps and common controls
without COM initialization or registry writes. CPath, CAtlFile,
CString and native Win32 synchronization are support utilities that
live in `include/` and do not link COM Client libraries.

**Excluded features:** implementing `IClassFactory` servers, registering
COM class objects, RGS, HKCR/HKLM registry writes, self-registering DLLs,
server-side Object Maps, in-proc/out-of-proc servers and ActiveX/OLE
container implementations. The broader original OpenATL headers may declare
such names, but this project does not qualify or ship those features.

## Upstream pins and licenses

- Clean-room MIT ATL source under `include/`, originally derived from
  `kkhaike/openatl@7b587f3e10f3f8d93a01522033de80007368bad5`.
  CI only compiles the checked-in headers and refuses an external ATL root.
- WTL10 MS-PL headers are installed by root `vcpkg.json` with builtin
  baseline `3037cbe976a6872e56e8b07152e19a8344360491`. The
  `ports/wtl` overlay pins version `10.0.10320#6`, retains upstream
  WTL10 source SHA512 and patches, and removes only the dependency on
  Microsoft's `atl` vcpkg stub.
- WTL sample `kkhaike/wtltest@63ba6a992cb3d79d56be66cd283f6615c6646b3d`.
- Project-specific glue/tests use Apache-2.0. RATL references, RGS scripts
  and LGPL/GPL patches have been removed from the active code path and files.
  Historical Git commits preserve earlier research, not project scope.
- Both llvm-mingw 20261020 and genuine MSVC 2022 build and test Windows
  x64+ARM64; WTL is manifest-installed on all toolchains including
  Linux/macOS Windows cross-builds. No Microsoft ATL include-path fallback.

The W^X thunk and test-only fault injection live in the checked-in
`atlwin.h`; no external checkout is patched. Actual CFG/CET
mitigations, callback teardown/unload races, interactive SDI and
production distribution are tracked separately.

See [toolchain matrix](TOOLCHAIN_MATRIX.md).
