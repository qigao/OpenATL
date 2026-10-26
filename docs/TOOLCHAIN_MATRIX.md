# OpenATL CI toolchain matrix

The **OpenATL** repository has two independent, blocking Windows GUI/COM
toolchain paths. No Microsoft ATL fallback and no TinyCC compiler
dependency. Both compile exactly pinned external **OpenATL MIT**
`7b587f3e10f3f8d93a01522033de80007368bad5` and the official
**WTL10 MS-PL** release `10.0.10320`, with qualification-only patches.

| Path | Compiler & host | Target | Validation |
| --- | --- | --- | --- |
| llvm-mingw | Windows x64, Clang++ 20261020 | x64 | native GUI, COM P0–P1C 5×, real full SDI RC/link |
| llvm-mingw | Windows x64 cross-build | ARM64 | PE/RC + SHA256 → true Windows ARM64 execution 5× |
| llvm-mingw | Linux and macOS | x64, ARM64 | independently cross-built Windows PE including full SDI/resource |
| MSVC | VS2022 HostX64→x64 `cl.exe`, Windows SDK `rc.exe` | x64 | CMake/CTest **6/6 x5**, full SDI/resource PE link |
| MSVC | VS2022 HostX64→ARM64 `cl.exe`, Windows SDK `rc.exe` | ARM64 | PE/RC + SHA256 → true Windows ARM64 GUI/COM execution 5× |

## Exact-head acceptance

At GitHub HEAD `5aba2298b63f572f0f15064ae7341096a4e0c7d8`,
[CI #40360213409](https://github.com/qigao/OpenATL/actions/runs/40360213409)
completed **7/7 SUCCESS** (the above jobs plus ARM64 native handoff).
MSVC was verified using VS2022 C++ tools `14.44.35207` and Windows SDK
`10.0.26100.0`; the environment action is commit-pinned
`ilammy/msvc-dev-cmd@0b201ec74fa43914dc39ae48a89fd1d8cb592756`.
Both build architectures use CMake Ninja. In the MSVC lane, qualification
EXEs link the MSVC runtime statically via /MT; in llvm-mingw they use
`-static`. Compiler identities, PE architecture and original
`.rsrc` sections must be verified, and ARM64 artifact SHA256 is matched
before any native run.

This matrix establishes **WTL10 hidden-window/dialog conformance and
a scoped COM subset**, not full Microsoft ATL, an interactive SDI test,
CFG/CET, concurrency-safe thunk unloading or product-grade registry/COM
installation. RATL is separately built with llvm-mingw on x64 only,
and its LGPL/GPL source or PE artifacts are excluded from OpenATL SDKs.

## Running locally

For MSVC Windows x64, open a VS2022 x64 Developer Command Prompt and
bootstrap sources with `pwsh scripts/windows-openatl-bootstrap.ps1`,
then run `pwsh scripts/windows-msvc-qualification.ps1 -TargetArchitecture x64`.
For MSVC ARM64, use a HostX64→ARM64 developer prompt with
`-TargetArchitecture arm64`; that step builds and hashes PE files,
not native-executes them on an x64 host. The CI ARM64 runner performs
the actual five native runs. For llvm-mingw, use the existing
`scripts/openatl-cmake-windows.ps1` and
`scripts/openatl-cmake-cross.sh` with pinned llvm-mingw.

Do not package or install test-only patched external headers;
source pins, library-specific licenses, and Draft review gates remain.
