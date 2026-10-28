# RATL sample compatibility audit

Source: `katahiromz/RATL` commit
`2e2efa182d7c6ee9037454d23780991fb8543084`, directory `samples/`.
These are RATL project examples, separate from Microsoft's ATL samples and
the official WTL release. This is a source audit; no OpenATL build/runtime
admission is claimed for the three applications.

| Sample | Existing OpenATL building blocks | Observed blocker |
| --- | --- | --- |
| AtlConsoleApp | `CSimpleArray`, standard namespace import | Global `CMyExeModule` derives from missing `CAtlExeModuleT` |
| AtlDialogApp | `CDialogImpl`, `CWindow`, message maps, modal dialogs, basic `CRegKey` | Missing `CAtlExeModuleT` |
| AtlWindowApp | `CWindowImpl`, `CWndClassInfo`, frame traits, paint/message handling, typed registry values | Missing `CAtlExeModuleT` and generic `CRegKey::QueryValue(name, type, data, bytes)` |

The two GUI sources define narrow `WinMain` while using TCHAR-based UI APIs.
Their eventual MinGW admission also needs the matching CRT entry selection;
OpenATL's current default for WIN32 GUI targets selects the Unicode CRT entry.
Source compatibility for an entry function and Unicode UI APIs are distinct.
Runtime tests would need to preserve/restore their `Software/ReactOS` settings;
the console example expects an Enter key after printing the array.

Each sample's original CMakeLists adds a `RATL` subdirectory and consumes its
directory variables. The pinned root RATL CMakeLists sets no RATL include paths
for MSVC, allowing that compiler's installed Microsoft ATL headers to supply
the implementation. An upstream MSVC build therefore does not qualify OpenATL.
Future admission must compile the original sample source/resources against
only OpenATL's source headers and `OpenATL::ATL`, preserving explicit provenance.

`CAtlExeModuleT` is a modern module-template compatibility gap, distinct from
the deliberately removed obsolete `CComModule`. Implementing only an empty
class to make these declarations compile would not implement its module
lifetime, startup, COM and server contracts. It remains unsupported until
those behaviors have a defined and tested scope. Generic registry queries
can be implemented independently through the existing Win32 registry owner.

The repository README declares LGPL 2.1 or later, and the individual sample
directories include their license text. The audit does not copy sample source,
RATL headers or implementation into OpenATL, or change any upstream notices.

Sources:

- [Pinned samples](https://github.com/katahiromz/RATL/tree/2e2efa182d7c6ee9037454d23780991fb8543084/samples)
- [Pinned RATL build configuration](https://github.com/katahiromz/RATL/blob/2e2efa182d7c6ee9037454d23780991fb8543084/CMakeLists.txt)
- [Pinned README and license declaration](https://github.com/katahiromz/RATL/blob/2e2efa182d7c6ee9037454d23780991fb8543084/README.md)
