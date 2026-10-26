# OpenATL UI + optional COM client architecture

Two independent functional paths share only pinned clean-room ATL headers:

```
Win32 GUI application
  └─ OpenATL::WTL ──> OpenATL::ATL ──> Win32 windows / messages / resources
       (NO COM apartment initialization; NO COM client link dependency)

Optional system COM consumer
  └─ OpenATL::COMClient ──> OpenATL::ATL + Windows ole32 / oleaut32 / uuid
       (caller owns CoInitializeEx / CoUninitialize; CLIENT ONLY)
```

`OpenATL::ATL` and `OpenATL::WTL` are header-only build configuration
targets. `OpenATL::COMClient` is an explicitly selected client dependency.
Win32 UI can use WTL windowing, dialogs, message maps and common controls
without COM initialization or registry writes.

**Excluded features:** implementing `IClassFactory` servers, registering
COM class objects, RGS, HKCR/HKLM registry writes, self-registering DLLs,
server-side Object Maps, in-proc/out-of-proc servers and ActiveX/OLE
container implementations. The broader original OpenATL headers may declare
such names, but this project does not qualify or ship those features.

## Upstream pins and licenses

- External clean-room OpenATL MIT headers from
  `kkhaike/openatl@7b587f3e10f3f8d93a01522033de80007368bad5`.
- Official WTL10 MS-PL headers, pinned
  `Win32-WTL/WTL@065ef5597383c1231bd4892cf8e94331b29bdb97`,
  SHA256 of ZIP:
  `13de5cd7ed41454b8f287e9bcc4525234c6ac8c85905e4d4fe1a0776709fd78f`.
- WTL sample `kkhaike/wtltest@63ba6a992cb3d79d56be66cd283f6615c6646b3d`.
- Project-specific glue/tests use Apache-2.0. RATL references, RGS scripts
  and LGPL/GPL patches have been removed from the active code path and files.
  Historical Git commits preserve earlier research, not project scope.
- Both llvm-mingw 20261020 and genuine MSVC 2022 build and test Windows
  x64+ARM64; no Microsoft ATL include-path fallback.

The temporary CI W^X thunk patch changes the pinned external source for
**qualification only**, not for an exported SDK. Actual CFG/CET
mitigations, callback teardown/unload races, interactive SDI and
production distribution are tracked separately.

See [toolchain matrix](TOOLCHAIN_MATRIX.md).
