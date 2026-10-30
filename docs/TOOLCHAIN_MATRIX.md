# OpenWTL / OpenATL: Windows-only native CI matrix

The supported automated **target** matrix is exactly two compilers × two
Windows architectures. `OpenWTL::WTL` is the only public CMake GUI target;
`openatl_atl` remains the in-tree ATL/COM backend. Neither uses Microsoft
ATL, a WTL package fallback, `Salts::Unicode`, or any external Unicode engine.

| Compiler | Windows target | Required evidence |
| --- | --- | --- |
| llvm-mingw 20261020 | x64 | Native GUI/COM/Win32 Unicode/SDI/GuidGen tests ×5; x64 PE/CFG/CET image checks |
| llvm-mingw 20261020 | ARM64 | Cross-built on Windows x64; verified ARM64 SHA256/PE, then real Windows ARM64 native GUI/COM/SDI ×5 |
| MSVC 2022 | x64 | Native GUI/COM/Win32 Unicode/GuidGen/MemDlg/SDI tests ×5; CFG/CET image checks |
| MSVC 2022 HostX64→ARM64 | ARM64 | Verified PE/SHA256 and real Windows ARM64 native GUI/COM/SDI ×5 |

The source-only Python/git provenance job may run on Ubuntu for lower cost,
but **does not compile or claim Linux/macOS product compatibility**.
x86/i686 and Linux/macOS llvm-mingw cross-host jobs are no longer part of
mandatory CI. The entire 11-example WTL10 source set remains archived in
`wtl/Samples`; several upstream projects require a 32-bit ABI and cannot be
claimed fully executable under the new x64/ARM64-only matrix. Historical
Win32 CI results remain historical, not current qualification requirements.

MSVC developer-shell action is pinned at
`ilammy/msvc-dev-cmd@0b201ec74fa43914dc39ae48a89fd1d8cb592756`.
MSVC uses the existing selected CRT, llvm-mingw uses the pinned compiler
and -static for native test artifacts. CMake refuses external ATL headers.
`OpenWTL::WTL` transitively exposes only repository WTL/ATL sources
and standard Windows UI/COM/registry import libraries.

All source code, native CTest, COM client/server activation, HKCU,
real ARM64, W^X, PE identity, and process-unload tests remain admitted.
Hosted x64 PE CETCOMPAT is **not** real hardware HSP qualification;
the independent manual
[`openatl-hsp-dedicated.yml`](../.github/workflows/openatl-hsp-dedicated.yml)
still needs a genuinely HSP-capable x64 Windows runner.
Unqualified printer-modal UI and detached WTL monitor-worker join
remain tracked separately.

There is no installed OpenATL/WTL SDK and no installer/consumer test
project. Downstream Studio integration uses its own native Windows CI.
