# Required OpenATL llvm-mingw and MSVC CI

This project qualifies **Windows UI without COM initialization** and
**optional COM Client** with two mandatory compiler paths. The same in-repository MIT clean-room OpenATL implementation and pinned
official MS-PL WTL10 headers are used.

| Toolchain | Build host | Target | Mandatory acceptance |
| --- | --- | --- | --- |
| llvm-mingw 20261020 | Windows x64 | Windows x64 | UI no-COM + file/string/sync/path utilities + COM Client 5×, WTL10 SDI/.rc link |
| llvm-mingw 20261020 | Windows x64 → Windows ARM64 | ARM64 | PE machine/SHA256 → genuine ARM64 UI/utilities/client 5× |
| llvm-mingw 20261020 | Linux and macOS | Windows x64 + ARM64 | independently cross-build GUI/utilities/client PE + SDI/.rc |
| MSVC 2022 `cl.exe` / SDK `rc.exe` | Windows x64 | x64 | CMake **10/10** CTests 5×, SDI/.rc |
| MSVC 2022 HostX64→ARM64 | Windows x64 | ARM64 | PE machine/SHA256 → genuine ARM64 UI/utilities/client 5× |

MSVC developer-shell action is pinned to
`ilammy/msvc-dev-cmd@0b201ec74fa43914dc39ae48a89fd1d8cb592756`.
MSVC build uses `/MT`; llvm-mingw uses `-static`. Neither selects
Microsoft ATL or a separate RATL provider. CMake refuses the retired
`OPENATL_ATL_INCLUDE_DIR` setting; its source root is always `include/`. The GUI target
`OpenATL::WTL` must not transitively link COM client libraries;
`OpenATL::COMClient` is linked explicitly only by a COM consumer.

[Historical dual-toolchain baseline](https://github.com/qigao/OpenATL/actions/runs/40361221782)
preceded the UI/client-only architectural refactor and included removed
COM server tests. Do **not** interpret it as evidence for the new boundary.
The new branch HEAD requires its own full CI acceptance.

Still unqualified: CFG/CET, aggressive failure injection and concurrent
thunk unloading, real interactive SDI and production release.

**Build boundary:** OpenATL is permanently source-tree-only. No install,
export, package configuration, Installed SDK, or installed-consumer test is
planned or permitted. Windows SDK in the matrix means Microsoft's target
OS headers and resource tools, **not** an OpenATL distributable.
