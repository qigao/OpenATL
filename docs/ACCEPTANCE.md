# Conformance and CI exit criteria

The standalone OpenTL CI must establish fresh exact-head evidence.
Historical green runs in `qigao/tcc` are migration references only.

| Gate | Native Windows x64 | Native Windows ARM64 | Linux/macOS |
| --- | --- | --- | --- |
| WTL10 hidden GUI and .rc | execute 5x | execute 5x on ARM host | PE cross-build |
| Full upstream WTL10 SDI + .rc | full PE link | PE machine/architecture | full PE link |
| OpenATL P0/P1/P1-B | execute 5x | execute 5x | cross-build |
| P1-C isolated HKCU test | execute 5x | execute 5x | cross-build |
| RATL reference and RGS P1-D | execute 5x, separate EXE | NOT qualified | x64 cross-build only |
| CFG/CET, unload races, interactive SDI | NOT qualified | NOT qualified | N/A |

Never infer Windows execution from Linux/macOS cross-compilation.
Never count static linking as confirmation that WTL GUI is interactive.
Persist provenance SHA256 for native ARM64 executable handoff.
Enforce a failure if any required fixture is skipped, stripped, or the
downloaded upstream SHA/version is different.

**PR status:** Draft until new repository's native/cross-host checks pass,
open remaining security issues, and confirm licensing. A passing POC is
not permission to merge or publish a production COM/GUI implementation.
