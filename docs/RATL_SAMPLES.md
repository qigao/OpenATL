# RATL sample compatibility

Source: `katahiromz/RATL` commit
`2e2efa182d7c6ee9037454d23780991fb8543084`, directory `samples/`.
The qualification build downloads that exact archive, verifies SHA256
`070b29862ae2c52bd023ea54b7cdbc61f3dbe8df70346d29ccbc0c51980e913b`,
and compiles the unchanged source/resources through OpenATL's own CMake targets.
Only `OpenATL::ATL` and each sample's resource directory supply dependencies.
RATL headers, implementation and upstream CMake configuration are not used.

| Sample | Behavior admitted locally on MinGW/MSVC x64 |
| --- | --- |
| AtlConsoleApp | Exact CSimpleArray output (3, 4, 5), Enter input and clean exit |
| AtlDialogApp | Resource controls/icon, both OK and Cancel paths, clean modal exit |
| AtlWindowApp | Load registry geometry through generic QueryValue, move/resize, save exact geometry, normal exit |

The GUI tests preserve/restore the values they exercise and remove newly
created empty application keys. No sample source or resource is rewritten.
The three samples also cross-build as ARM64 PE/resource images. RATL sample
binaries are excluded from CI artifact uploads; their ARM64 native behavior
is not claimed. The native ARM64 fixture independently tests the module APIs.

The former gaps are filled by:

- `CAtlModule`, `CAtlModuleT<T>` and `CAtlExeModuleT<T>` with real lifetime and
  COM behavior; see [module protocol and limits](ATL_MODULE.md).
- Raw `CRegKey::QueryValue(name, type, data, bytes)` using RegQueryValueEx.
  Raw string bytes retain Win32 semantics; use QueryStringValue for checked
  terminated text. The deprecated QueryValue overloads remain absent.
- Undefining the windowsx.h SubclassWindow helper collision in atlwin.h.
- Per-target `OPENATL_USE_WINMAIN=TRUE` selects the narrow WinMain CRT entry
  for the two GUI samples on MinGW, while UI APIs remain Unicode. The default
  remains the wide entry for ordinary Unicode WTL GUI targets.

The module tests separately verify locks, LIFO/capacity-bounded termination,
object pins through destruction, S_OK/S_FALSE/failed COM initialization,
custom apartment hooks, class factory registration/lookup/revocation, Run
cleanup after failure, thread affinity and bounded command parsing. Creating
an EXE module initializes COM as the application opts into this module API;
ordinary WTL UI tests still complete without initializing COM.

Persistent EXE/AppID/type-library registration, automatic object maps,
idle-shutdown monitoring, services and a complete ATL EXE-server framework
remain outside this subset. Default registration commands fail E_NOTIMPL;
they never claim an installation succeeded. CComModule remains removed.

The upstream MSVC build leaves RATL include paths empty and can select
Visual Studio's ATL implementation, so its own build is not OpenATL evidence.
The repository README declares LGPL 2.1 or later; individual sample directories
retain their license files. Samples stay in the external build/download tree;
no RATL source is copied into OpenATL's implementation or relicensed.

Sources:

- [Pinned samples](https://github.com/katahiromz/RATL/tree/2e2efa182d7c6ee9037454d23780991fb8543084/samples)
- [Pinned upstream build configuration](https://github.com/katahiromz/RATL/blob/2e2efa182d7c6ee9037454d23780991fb8543084/CMakeLists.txt)
- [Pinned README/license declaration](https://github.com/katahiromz/RATL/blob/2e2efa182d7c6ee9037454d23780991fb8543084/README.md)
