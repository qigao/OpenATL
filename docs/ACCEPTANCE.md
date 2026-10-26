# Conformance and CI exit criteria

The standalone OpenATL CI must establish fresh exact-head evidence.
Historical green runs in `qigao/tcc` are migration references only.

| Gate / compiler | Windows x64 | Windows ARM64 | Linux/macOS |
| --- | --- | --- | --- |
| WTL10 hidden GUI + `.rc` / llvm-mingw | 5× native | 5× native | x64/ARM64 PE |
| WTL10 hidden GUI + `.rc` / MSVC | 5× native CTest | 5× native (x64→ARM64) | N/A |
| Full official WTL10 SDI + `.rc` / both | link, verify `.rsrc` | link, verify `.rsrc` | llvm-mingw PE only |
| OpenATL COM P0/P1/P1-B/P1-C / llvm-mingw | 5× native | 5× native | x64/ARM64 PE |
| OpenATL COM P0/P1/P1-B/P1-C / MSVC | 5× native CTest | 5× native + PE/SHA256 | N/A |
| RATL reference and RGS P1-D | llvm-mingw x64 5×, separate EXE | NOT qualified | x64 cross-build only |
| CFG/CET, unload races, interactive SDI | NOT qualified | NOT qualified | N/A |


The build must prove the selected provider is the **MIT clean-room OpenATL**
header, not MSVC's built-in ATL; the CMake configure step checks its marker.
MSVC and llvm-mingw use independently built PE binaries and native execution
results. MSVC x64/ARM64 C++ qualification uses /MT and GNU/Clang uses
-static only; never forward GCC linker switches to MSVC.

Never infer Windows execution from Linux/macOS cross-compilation.
Never count static linking as confirmation that WTL GUI is interactive.
Persist provenance SHA256 for native ARM64 executable handoff.
Enforce a failure if any required fixture is skipped, stripped, or the
downloaded upstream SHA/version is different.

**PR status:** Draft until new repository's native/cross-host checks pass,
open remaining security issues, and confirm licensing. A passing POC is
not permission to merge or publish a production COM/GUI implementation.
