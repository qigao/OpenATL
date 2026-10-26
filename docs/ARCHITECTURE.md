# OpenATL architecture

## Libraries and ownership

OpenATL is a standalone **WTL + ATL Windows GUI compatibility project**,
not a compiler. Dependency layers:

```
Windows GUI application
  -> OpenATL::WTL (official WTL10 headers, externally obtained, MS-PL)
  -> OpenATL::ATL (pinned OpenATL headers, externally obtained, MIT)
  -> Windows COM / Win32 APIs and OS ABI
```

The CMake targets do not own or expose RATL C++ objects. RATL is a
separate **x64-only** LGPL-licensed behavior reference for COM identity,
object factories and registry/RGS scripts. No include-path fallback:
never mix OpenATL and RATL headers in a translation unit or module.

TinyCC's compiler implementation and portability remain owned by `qigao/tcc`.
OpenATL consumes **either** a Windows-targeting Clang++/llvm-mingw toolchain
**or** MSVC `cl.exe`, each compiling the pinned clean-room OpenATL provider.
Microsoft ATL headers are not used as an implicit fallback. MSVC uses
Visual Studio 2022 `cl.exe` plus Windows SDK `rc.exe` in separate x64 and
HostX64→ARM64 builds. See [toolchain matrix](TOOLCHAIN_MATRIX.md).

## Version and licensing pins

- WTL10: `Win32-WTL/WTL@065ef5597383c1231bd4892cf8e94331b29bdb97`
  `Releases/WTL10_10320_Final.zip` SHA256
  `13de5cd7ed41454b8f287e9bcc4525234c6ac8c85905e4d4fe1a0776709fd78f`,
  Windows Template Library / **MS-PL**.
- OpenATL: `kkhaike/openatl@7b587f3e10f3f8d93a01522033de80007368bad5`,
  **MIT**, header subset (not Microsoft ATL).
- WTL demo: `kkhaike/wtltest@63ba6a992cb3d79d56be66cd283f6615c6646b3d`;
  full SDI+RC link is tested, interactive GUI execution is unqualified.
- RATL: `katahiromz/RATL@2e2efa182d7c6ee9037454d23780991fb8543084`,
  **LGPL-2.1-or-later** for statreg.h and repo, some files carry their
  own GPL notices. Reference PE executables are NOT OpenATL deliverables.
- llvm-mingw: UCRT build `20261020` pinned by per-host SHA256 in CI.
- MSVC: CI checks genuine VS2022 `cl.exe` and SDK `rc.exe`
  (initial acceptance observed MSVC tools `14.44.35207` and SDK
  `10.0.26100.0`). Developer-shell action pinned to commit
  `ilammy/msvc-dev-cmd@0b201ec74fa43914dc39ae48a89fd1d8cb592756`.
  No Microsoft ATL provider substitution or RATL ARM64 claim.

## Acceptance branches versus product

P0: `IUnknown` identity, COM Map and basic RAII.
P1-A: class factories, construction failure cleanup, lifecycle.
P1-B: temporary two-TU `OBJECT_ENTRY_AUTO` map, bounded registration,
      failure rollback, wrong-thread rejection.
P1-C: isolated HKCU sandbox (with process-local HKCR override), test-only
      registration and controlled abrupt-child recovery.
P1-D: **separate RATL** RGS regression (original false-success, test-only
      exact-pinned fix, injected permission denial and preservation).

The checked-in `scripts/openatl-*-hardening.py` and
`scripts/ratl-rgs-nested-test-patch.py` are explicit, exact-SHA
**qualification-only** patches. They MUST NOT be exported as the
production OpenATL ATL implementation: W^X thunk CFG/CET, concurrency,
modular unload and complete COM server/type-library support remain open.

The OpenATL project itself is Apache-2.0. External source licenses remain
unmodified. RATL/ReactOS code snippets in the test patch are marked
LGPL-2.1-or-later and must be considered separately from Apache-2.0.
