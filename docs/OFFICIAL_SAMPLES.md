# Official sample coverage

Official samples are a separate acceptance boundary from the community SDI
fixture (`kkhaike/wtltest`) and OpenATL's own conformance tests. Passing that
community fixture does not qualify the official WTL or Microsoft ATL suites.

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
| MemDlg | Blocked by llvm-rc `TOOLBAR` resource syntax; not admitted | Build + native x64 test passed | Resource About dialog, in-memory About dialog, text/buttons, normal frame exit |

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
require exact SHA256/PE/resource manifests (19 files / 23 cases for llvm-mingw,
20 files / 24 cases for MSVC). Linux/macOS cross jobs build and inspect GuidGen;
cross-building does not count as runtime admission. Current-head CI must pass
before claiming these remote/native ARM64 results.

The remaining release samples (Aero, Alpha, BmpView, MDIDocVw, MTPad, MTPad7,
TabBrowser, Wizard97Test and WTLExplorer) are not qualified by this pass.
In particular, no blanket claim of all official sample compatibility is made.

## Microsoft's ATL samples

The source audit uses `microsoft/VCSamples` commit
`9e1d4475555b76a17a3568369867f1d7b6cc6126`, under
`VC2010Samples/ATL/General/`. These are official archived samples, not passing
OpenATL build/runtime tests:

| Sample | Observed dependencies still missing in OpenATL |
| --- | --- |
| COMMap | `CAtlDllModuleT`, `IDispatchImpl`, automatic object maps, tear-offs, aggregation and chained COM-map entries |
| ATLCollections | `CAtlDllModuleT`, `IDispatchImpl`, `ICollectionOnSTLImpl`, `CComEnumOnSTL`, registrar and generated/imported type libraries |
| ATLSafeArray | `CAtlDllModuleT`, `atlsafe.h` / `CComSafeArray`, `atlctl.h`, dispatch and type-library support |

Adding `atlcom.h` alone does not implement these contracts. The implemented
nonaggregated object/map subset remains covered by OpenATL's native tests;
see [ATL compatibility](ATL_COMPATIBILITY.md). `CComModule` remains excluded
as requested; these additional modern APIs would require separate work.

Sources:

- [Official WTL release](https://sourceforge.net/projects/wtl/files/WTL%2010/WTL%2010.0.10320%20Release/)
- [Microsoft sample catalog](https://learn.microsoft.com/en-us/cpp/overview/visual-cpp-samples)
- [Pinned Microsoft ATL sample sources](https://github.com/microsoft/VCSamples/tree/9e1d4475555b76a17a3568369867f1d7b6cc6126/VC2010Samples/ATL/General)
