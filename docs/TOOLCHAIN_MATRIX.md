# Required OpenATL llvm-mingw and MSVC CI

This project qualifies **Windows UI without COM initialization** and
**optional COM Client** plus an independent opt-in in-process COM server
with two mandatory compiler paths. The same in-repository MIT clean-room OpenATL implementation and pinned
official MS-PL WTL10 headers are used, installed by the checked-in
vcpkg manifest and ATL-free WTL overlay on every build host.

| Toolchain | Build host | Target | Mandatory acceptance |
| --- | --- | --- | --- |
| llvm-mingw 20261020 | Windows x64 | Windows x64 | 22 CTests 5×: UI/utilities/client, strict Unicode + UTF-8 ACP, server core + STA/MTA activation, unload/SDI; CFG/CET image checks, hosted HSP runtime excluded |
| llvm-mingw 20261020 | Windows x64 → Windows ARM64 | ARM64 | PE machine/SHA256 → genuine ARM64 UI/utilities/client 5× |
| llvm-mingw 20261020 | Linux and macOS | Windows x64 + ARM64 | independently cross-build GUI/utilities/client PE + SDI/.rc |
| MSVC 2022 `cl.exe` / SDK `rc.exe` | Windows x64 | x64 | 24 CTests 5×, including Unicode, server core + STA/MTA activation, HSP, unload and SDI; separate published Salts Unicode SDK profile |
| MSVC 2022 HostX64→ARM64 | Windows x64 | ARM64 | PE machine/SHA256 → genuine ARM64 UI/utilities/client 5× |

MSVC developer-shell action is pinned to
`ilammy/msvc-dev-cmd@0b201ec74fa43914dc39ae48a89fd1d8cb592756`.
MSVC build uses `/MT`; llvm-mingw uses `-static`. Neither selects
Microsoft ATL or a separate RATL provider. CMake refuses the retired
`OPENATL_ATL_INCLUDE_DIR` setting; its source root is always `include/`. GUI targets link `OpenATL::ATL` and include vcpkg-installed WTL headers.
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

**Build boundary:** OpenATL is permanently source-tree-only. There is
no OpenATL install/export/package config, Installed SDK, or installed-consumer
test. External WTL headers are installed **by vcpkg** under
`vcpkg_installed/<triplet>/<triplet>/include`; that is not an OpenATL SDK.
Windows SDK means Microsoft's target OS headers and resource tools.
