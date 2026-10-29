# WTL 10.0.10320#7 — original vcpkg source only (Issue #44)

## Single source and runtime ownership

[WTL #7](https://github.com/qigao/vcpkg-cache/pull/237) is merged into
`qigao/vcpkg-cache master@8359cdbdf25c1f58cfc7230cc6318dc2fdb804fb`.
OpenATL's CMake and GitHub Actions pin that **exact commit**. The
original WTL release archive SHA512 and source revision remain fixed;
the port applies only narrowly audited Clang compatibility corrections.
All 20 public headers, 11 official examples, AppWizard and MS-PL are kept.

In this Draft branch, `OpenATL::ATL` obtains WTL headers **directly from
the installed vcpkg package** in every build. The build-local
`PrepareWtl.cmake` and copied/modified WTL header tree are retired.
`OPENATL_WTL_FULL_ORIGINAL=ON` is now the mandatory default, and
`OFF` fails rather than selecting an alternate provider. No Microsoft ATL,
SourceForge fallback or Installed OpenATL SDK exists.

## Verified original-source qualifications

- [CI #40846739457](https://github.com/qigao/OpenATL/actions/runs/40846739457):
  original WTL builds every unchanged official Win32 sample under MSVC
  and llvm-mingw, CTest x5.
- [CI #40850856167](https://github.com/qigao/OpenATL/actions/runs/40850856167):
  zero-adapter WTL native x64 CTest x5 on both MSVC and llvm-mingw.
- [CI #40852048002](https://github.com/qigao/OpenATL/actions/runs/40852048002):
  8/8 SUCCESS; original and adapted ARM64 artifacts independently verify
  SHA256/PE/resources and real Windows ARM64 UI, COM and native lifecycles
  x5 each for MSVC and llvm-mingw.
- This Draft's new default-original build must pass the same complete
  CI matrix again, with no skipped tests or rewritten source APIs.

The optional `OPENATL_BUILD_WTL_UNMODIFIED=ON` executable adds
`CComModule` legacy class-object ownership and COM registration/rollback,
original `CAppModule` message loops, `CPrintDialogEx` COM callback/site,
`CRegPropertyImpl<T>` DWORD/bool/Unicode/binary/char-array/HFONT
serialization and bounded buffer errors, and genuine `CHyperLink`
Win32 controls. Isolated HKCU registry testing uses
`RegOverridePredefKey` only in the test process and restores it
before deleting its GUID-owned key.

`tests/wtl10/original_arm64_manifest.py` refuses a non-original
CMake cache or copied WTL headers, validates the ARM64 PE and resources,
and produces exact SHA256 provenance for the separately built binaries.
`native_arm64.py` refuses non-native Windows ARM64 runners.

## Remaining real product blockers — PR #45 stays Draft

The original `WTL::CServerAppModule::StartMonitor()` discards its
worker handle. Its non-atomic `m_bActivity`, concurrent
`Unlock()/Term()`, shutdown event lifetime and worker ownership do not
provide a safe join or unload proof. Existing `server_monitor.cpp` only
qualifies an explicitly retained worker with bounded join, not the
original detached StartMonitor/Unlock/Term schedules.

The actual Win32 `PrintDlgEx` printer-window and selection lifecycle
also remains unqualified; COM callback/source tests cannot stand in
for real printer UI acceptance. These two gates remain open under
[Issue #44](https://github.com/qigao/OpenATL/issues/44); the Draft
PR is not to be merged or released on source-build evidence alone.

## Reproduce

Install `wtl@10.0.10320#7` from the fixed vcpkg-cache overlay, then
set `VCPKG_INSTALLED_DIR`/`VCPKG_TARGET_TRIPLET` (or the OpenATL
source-tree env equivalents) and run:

```powershell
cmake --preset msvc-x64 -DOPENATL_BUILD_WTL_UNMODIFIED=ON
cmake --build --preset msvc-x64 --parallel 2
ctest --preset msvc-x64 --repeat until-fail:5 --output-on-failure
```

## Original ARM64 provenance contract (strict version 1)

Each independently compiled `*-original` ARM64 artifact now carries a
versioned `manifest.json` with an exact `provenance` object and a separate
`files` SHA256 map. The producer reads the **installed** WTL port identity
stamp after CMake's source gate and records the checked-out CI commit,
compiler (`llvm` or `msvc`), WTL version, provider, and archive SHA512.

On the **real Windows ARM64** runner, the existing five-pass test suite now
rejects missing/extra files, incorrect digest syntax, duplicate manifest
keys, missing/wrong source metadata, a different checkout, or cross-toolchain
artifact substitution *before executing any binary*. The legacy flat
manifest format remains valid only for the non-`original` artifact set.
The source-only job runs six negative-contract tests without claiming
printer UI or detached StartMonitor ownership is qualified.
