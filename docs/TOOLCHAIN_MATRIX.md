# Required OpenATL llvm-mingw and MSVC CI

This project qualifies **Windows UI without COM initialization** and
**optional COM Client** plus an independent opt-in in-process COM server
with two mandatory compiler paths. The same in-repository MIT clean-room OpenATL implementation and pinned
official MS-PL WTL10 headers are obtained by CMake FetchContent and adapted
in each build tree on every build host. Downstream ports are not required.

| Toolchain | Build host | Target | Mandatory acceptance |
| --- | --- | --- | --- |
| llvm-mingw 20261020 | Windows x64 | Windows x64 | 27 CTests 5×: UI/utilities/client, Unicode, server core + STA/MTA activation, unload/community SDI, official GuidGen and three RATL examples; CFG/CET image checks, hosted HSP runtime excluded |
| llvm-mingw 20261020 | Windows x64 → Windows ARM64 | ARM64 | PE machine/SHA256 → genuine ARM64 UI/utilities/client 5× |
| llvm-mingw 20261020 | Linux and macOS | Windows x64 + ARM64 | Linux cross-build required; macOS runner currently unavailable and explicitly unqualified until `OPENATL_MACOS_RUNNER_AVAILABLE=true` and exact-head cross-build passes (#25) |
| MSVC 2022 `cl.exe` / SDK `rc.exe` | Windows x64 | x64 | Native Unicode, server core + STA/MTA activation, unload/community SDI, official GuidGen/MemDlg and three RATL examples (5×); CFG/CET PE metadata, no generic-runner HSP claim; separate Salts Unicode SDK profile |
| MSVC 2022 HostX64→ARM64 | Windows x64 | ARM64 | PE machine/SHA256 → genuine ARM64 UI/utilities/client 5× |

MSVC developer-shell action is pinned to
`ilammy/msvc-dev-cmd@0b201ec74fa43914dc39ae48a89fd1d8cb592756`.
MSVC build uses `/MT`; llvm-mingw uses `-static`. Neither selects
Microsoft ATL or a separate RATL provider. CMake refuses the retired
`OPENATL_ATL_INCLUDE_DIR` setting; its source root is always `include/`. GUI targets link `OpenATL::ATL`, which supplies both ATL and WTL headers
and common UI/COM system link dependencies.
There is no `OpenATL::WTL` target, alias or fallback to Microsoft ATL;
`OpenATL::ATL` supplies the COM/registry system link dependencies. Applications
explicitly initialize their COM apartment and invoke server/registration APIs.

[Historical dual-toolchain baseline](https://github.com/qigao/OpenATL/actions/runs/40361221782)
preceded the UI/client-only architectural refactor and included removed
COM server tests. Do **not** interpret it as evidence for the new boundary.
The new branch HEAD requires its own full CI acceptance.

Native mitigation, bounded unload and interactive SDI evidence is scoped to
the formal fixtures. The server adds explicit nonaggregated class factories,
module locks and registry ownership tests; it does not provide legacy ATL
server compatibility. See [server contracts](COM_SERVER.md) and
[hardware HSP scope](CET_HARDWARE_ACCEPTANCE.md). Product release acceptance
remains application-specific.

**Current build boundary:** OpenATL is presently consumed via source-tree
CMake, without installed/exported config or installed-consumer CI. This is
today's implemented build, not a permanent prohibition. An installed profile
can be evaluated and qualified under #4. WTL sources remain checksum-pinned;
the build-local adapted headers live under
`<OpenATL binary directory>/wtl/include`.
Windows SDK means Microsoft's target OS headers and resource tools.

RATL examples are rebuilt from pinned external source and tested on x64; their
binaries are excluded from artifact uploads. ARM64 admission includes the
separate native module-contract case (LLVM 24 cases / MSVC 25 cases), while
RATL examples receive ARM64 build/resource checks only.
