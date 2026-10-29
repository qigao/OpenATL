# Official sample coverage

The full WTL 10.0.10320 source archive and its eleven unchanged official
samples are now vendored at
[`third_party/wtl/WTL10_10320_Final.zip`](../third_party/wtl/WTL10_10320_Final.zip).
CMake verifies its **repository-owned SHA512** and extracts it locally;
it never downloads SourceForge at configure time. Original `MS-PL.txt`
remains inside the source archive and is copied into the build tree. The
mirror ZIP's SHA512 differs from the previous SourceForge ZIP; this is
documented explicitly in [WTL provenance](../third_party/wtl/README.md).
Full official source compilation and native UI tests remain mandatory.

Official samples are a separate acceptance boundary from the community SDI
fixture (`kkhaike/wtltest`) and OpenATL's own conformance tests. Passing that
community fixture does not qualify the official WTL or Microsoft ATL suites.

## Acceptance contract: ATL and WTL, without MFC

The requested acceptance boundary is all 11 samples in WTL 10.0.10320 and
every pure ATL project in the pinned Microsoft `VC2010Samples/ATL` directory,
on both MSVC and MinGW. Preserve the public ATL/WTL APIs and compile the
original sample sources and resources. The WTL Win32 build boundary has passed;
the Microsoft pure ATL boundary has **not yet passed**.
MFC implementations, headers and libraries are not dependencies of OpenATL.
Projects explicitly configured to use MFC are recorded separately, excluded
from pure ATL totals, and never counted as passing pure ATL tests.

The reference for public ATL signatures is MSVC 14.44.35207 (VS 2022).
Its ATL/MFC implementation is not a build dependency: MSVC qualification
uses `/X` and explicitly enumerates compiler/Windows SDK include directories
without `atlmfc`, so a missing repository header produces a real failure.

The original release projects target Win32. The full WTL build is selected by
the `msvc-x86` and `llvm-i686` presets (`OPENATL_BUILD_OFFICIAL_SUITE=ON`).
MTPad's callback signatures and Wizard97Test's return types depend on that
32-bit ABI. Existing x64/ARM64 profiles remain regression profiles; passing
them does not replace the full Win32 suite.

Use the VS 2022 x86 developer environment for `msvc-x86`. For `llvm-i686`,
provide `OPENATL_LLVM_MINGW_BIN` and `OPENATL_WINDOWS_SDK_BIN` (the versioned
Windows SDK `bin/<version>/x86` directory). That profile uses SDK RC for the
original TOOLBAR syntax and SDK UICC for Ribbon BML generation, with no MFC.
UICC generates the missing BML in the build directory; archived `.cpp`, `.h`
and `.rc` files remain unchanged.

For MinGW, the selected SDK's three Ribbon declaration headers are adapted in
the build directory and supplied through `OpenATL::ATL`, including GUIDs and
the Property System import library. No consumer-specific Ribbon include roots,
additional OpenATL library or Microsoft ATL/MFC implementation is required.
These Windows SDK headers remain build inputs under the SDK's license.

Wizard97Test contains Windows-1252 characters in wide string literals. MSVC
uses an explicit source charset; Clang's compiler input reader decodes that
one file to UTF-8 in memory, preserving the original bytes on disk, C++ text,
line locations and include resolution. This is character decoding, not a
sample token/API patch. The build-local WTL adapter adds explicit control
conversions, CRTP `template`/constant qualifications, and copies input-only
values for MinGW SDK signatures that incorrectly lack const qualifiers.

Local full-suite build results (2026-10-28):

| Sample | MSVC 14.44 Win32 | llvm-mingw Win32 |
| --- | --- | --- |
| Aero | Built | Built |
| Alpha | Built | Built |
| BmpView | Built | Built |
| GuidGen | Built | Built |
| MDIDocVw | Built | Built |
| MemDlg | Built | Built with Windows SDK RC |
| MTPad | Built | Built |
| MTPad7 | Built | Built with SDK RC/UICC/Ribbon declarations |
| TabBrowser | Built | Built |
| Wizard97Test | Built | Built |
| WTLExplorer | Built | Built |

Both compilers have compiled and linked all 11 examples. Individual successful
links are compile evidence only; no blanket runtime qualification of every
example is claimed. Native conformance separately exercises real WebBrowser
activation/navigation, typed connection-point events, source identity,
argument errors, exception translation, window teardown, retained-host module
locks, disconnected queries, wrong-thread rejection and COM balance.

The Microsoft inventory is
[`microsoft-projects.json`](../tests/official_samples/microsoft-projects.json):
48 projects, of which 11 declare static/dynamic MFC and 37 do not. All 37
remain unqualified until their original sources, code generation and resources
are integrated into both compiler build graphs. The inventory records scope,
not success. In particular, MIDL, `#import`, registrar, dispatch, ActiveX server/composite controls and
OLE DB requirements are outstanding work within the pure ATL contract, rather
than reasons to silently omit those projects.

## WTL 10.0.10320 release

The sources/resources below come directly from the `Samples/` directory in the
same official SourceForge release whose SHA512 is pinned by OpenATL. They are
built unchanged through `tests/official_samples/CMakeLists.txt`, linked only
to `OpenATL::ATL`, and driven by native behavior tests registered in CTest.
The build supplies the common-controls v6 manifest as a separate resource;
upstream source files are never rewritten.

| Official sample | llvm-mingw | MSVC | Behavior verified |
| --- | --- | --- | --- |
| GuidGen | Build + native x64 test passed | Build + native x64 test passed | New GUID differs; four formats preserve its value; About controls; normal close |
| MemDlg | Build + native Win32 test passed using SDK RC; llvm-rc still lacks TOOLBAR support | Build + native x64 test passed | Resource About dialog, in-memory About dialog, text/buttons, normal frame exit |

GuidGen's normal close persists one user setting. Its test saves/restores the
preexisting `GUID Type` value and removes only newly created empty keys. It
does not invoke Copy or change the clipboard. MemDlg explicitly initializes
COM in its original application entry point; its success is not evidence of
a no-COM application. Neither sample modifies its original initialization.

These samples exposed fixes owned by OpenATL and its build-local WTL adapter:

- Standard default `using namespace ATL`, with `_ATL_NO_AUTOMATIC_NAMESPACE`
  as the opt-out.
- Preserve WTL's real `CRegKeyEx` alias to `ATL::CRegKey`.
- Use a type-qualified CRTP enum for command-bar constant array bounds on Clang.
- Apply the documented Windows 7 minimum before Windows SDK inclusion, including
  when historical sample headers specify older versions. This is source
  compatibility with those samples, not a Windows XP runtime-support claim.

CI builds GuidGen for x64/ARM64 on both compilers and MemDlg for x64/ARM64 on
MSVC; the native Windows jobs run their behavior tests five times. ARM64 tests
require exact SHA256/PE/resource manifests (20 files / 27 cases for llvm-mingw,
21 files / 28 cases for MSVC). Linux/macOS cross jobs build and inspect GuidGen;
cross-building does not count as runtime admission. Current-head CI must pass
before claiming these remote/native ARM64 results.

The table above records the expanded compilation coverage. Runtime coverage
in this section remains limited to GuidGen and MemDlg.

## Pinned original Microsoft COMMap MIDL milestone

The repository now retains the **unchanged** `General/commap/COMMap.idl`
from `microsoft/VCSamples@9e1d4475555b76a17a3568369867f1d7b6cc6126`,
with Git blob SHA `dcf19ff69001c6f2f242d15c917a5598b03d70ba`
checked at CMake configure time on **all hosts**. The file is under
`tests/official_samples/microsoft_atl/commap/COMMap.idl`, with the
upstream MIT license and explicit Git checkout byte-preservation policy.

On **actual Windows** hosts, the selected Windows SDK `midl.exe` is a
required build tool. It generates `COMMap.h`, `COMMap_i.c`,
and a true binary `COMMap.tlb` from this exact original IDL.
The GUID implementation and generated header compile into the native
COM test executable under both MSVC and llvm-mingw against repository-owned
OpenATL headers. CTest loads the MIDL-generated TypeLib using
`LoadTypeLibEx(REGKIND_NONE)`, verifies the real COMMap LIBID/version
and original `IChain`/`IOuter` IID/member-name DISPIDs.
There is no registry installation or `regsvr32` post-build action.

Linux/macOS cross-host builds cannot run Windows SDK MIDL; they verify
original source provenance and remain required for existing Windows PE
cross-build tests. This does **not** qualify MIDL on Linux/macOS.

This qualifies original **IDL code generation and binary TypeLib loading**,
not original COMMap project compilation/link or native class activation.
The unchanged COMMap still needs aggregation, cached tear-offs, automatic
aggregation, TypeLib registration, broader `IDispatchImpl` and
generated `aggreg/aggreg.idl`. All 37 original pure-ATL project
entries remain **not-qualified**.

## Original ATL RGS grammar fixtures (not sample-build evidence)

The exact, unchanged Chain/Outer, ArrayManager and MapCollection `.rgs`
resources from the pinned Microsoft ATL tree are now separately admitted
by a read-only, bounded RGS parser. Their original Git blob SHA-1 values
and MIT license are recorded in
[`tests/atl_rgs/README.md`](../tests/atl_rgs/README.md). Native Windows
COM tests compile these samples as wide string resources and check grammar,
key/value projection, `%MODULE%` substitution and fail-closed malformed
documents. Parsing does not execute registry instructions or qualify
the 37 original Microsoft pure-ATL applications.

## Microsoft's ATL samples

The source audit uses `microsoft/VCSamples` commit
`9e1d4475555b76a17a3568369867f1d7b6cc6126`, under
`VC2010Samples/ATL/` (all subdirectories). These are official archived samples, not passing
OpenATL build/runtime tests:

| Sample | Observed dependencies still missing in OpenATL |
| --- | --- |
| COMMap | `CAtlDllModuleT`, `IDispatchImpl`, automatic object maps, tear-offs, aggregation and chained COM-map entries |
| ATLCollections | `CAtlDllModuleT`, `IDispatchImpl`, `ICollectionOnSTLImpl`, `CComEnumOnSTL`, registrar and generated/imported type libraries |
| ATLSafeArray | `CAtlDllModuleT`, `atlsafe.h` / `CComSafeArray`, `atlctl.h`, dispatch and type-library support |

Adding `atlcom.h` alone does not implement these contracts. The implemented
nonaggregated object/map subset remains covered by OpenATL's native tests;
see [ATL compatibility](ATL_COMPATIBILITY.md). `CComModule` is not currently implemented, but no standard ATL API is
excluded merely because of the obsolete UI-only scope decision. Source
compatibility with the original pure ATL samples is the target.

Sources:

- [Official WTL release](https://sourceforge.net/projects/wtl/files/WTL%2010/WTL%2010.0.10320%20Release/)
- [Microsoft sample catalog](https://learn.microsoft.com/en-us/cpp/overview/visual-cpp-samples)
- [Pinned Microsoft ATL sample sources](https://github.com/microsoft/VCSamples/tree/9e1d4475555b76a17a3568369867f1d7b6cc6126/VC2010Samples/ATL/General)
