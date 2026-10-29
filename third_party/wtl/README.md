# Vendored Windows Template Library 10.0.10320

OpenATL tracks the **complete WTL 10.0.10320 source release**, including
original `Include/`, all eleven official `Samples/`, and the
`MS-PL.txt` license, as an immutable compressed source archive:

`WTL10_10320_Final.zip` (718,708 bytes)

- Release: WTL 10.0.10320 (original project release from November 2020)
- Archive source: `Win32-WTL/WTL` GitHub mirror,
  `Releases/WTL10_10320_Final.zip`
- Mirror Git commit: `065ef5597383c1231bd4892cf8e94331b29bdb97`
- Exact binary Git blob SHA-1: `a9f9d84495aac2155b0fdb00fec0e421b0e75c90`
- SHA512: `1890711aac1ade673fb071da2f52e2c7bb30fe394b7db735ff74b4578c8c8eea8bd470924dbad013a567d0cc9b97d865060d72ea6d7330d393af5b20b1e09970`
- License: Microsoft Public License (MS-PL), preserved verbatim **inside** the archive as `MS-PL.txt`; copied to the CMake build tree.

**Important provenance distinction:** This mirror archive is *not* the
byte-identical SourceForge `WTL10_10320_Release.zip` previously used by
OpenATL. That archive's SHA512 was
`086a6cf6a49a4318a8c519136ba6019ded7aa7f2c1d85f78c30b21183654537b3428a400a64fcdacba3a7a10a9ef05137b6f2119f59594da300d55f9ebfb1309`.
Do not describe these two ZIP artifacts as identical or silently substitute
one for the other. This repository deliberately selects **one** GitHub
mirror archive, with its own exact SHA512/Git blob identity, and qualifies
the extracted WTL 10 sources through the *unchanged* official native
WTL10 sample and application tests.

`cmake/OpenATLWTL.cmake` verifies the archive's hash and expands it
**locally** with CMake FetchContent. No SourceForge, GitHub, network fetch,
installer, second WTL provider or global WTL include path is used during
configuration. The preexisting `cmake/wtl/PrepareWtl.cmake` applies OpenATL
compatibility adaptations **to the build directory only**, leaving this
archived source untouched.

`tests/wtl_dependency_policy.py` verifies the tracked WTL source, SHA512,
ZIP member CRCs, license and official sample roster. CMake re-verifies
SHA512 for every configuration, including Windows and Linux cross-builds.
A mismatch is always fatal; there is no fallback to downloading WTL.

WTL updates require a distinct source review: preserve upstream license,
record the new exact Git binary blob + SHA512, rerun the complete original
WTL application matrix and native COM Client/Server tests. The current
acceptance deliberately **does not upgrade** WTL to the newer 10.01 release.
