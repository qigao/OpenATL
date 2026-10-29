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
- SHA512: `086a6cf6a49a4318a8c519136ba6019ded7aa7f2c1d85f78c30b21183654537b3428a400a64fcdacba3a7a10a9ef05137b6f2119f59594da300d55f9ebfb1309`
- License: Microsoft Public License (MS-PL), preserved verbatim **inside** the archive as `MS-PL.txt`; copied to the CMake build tree.

**Verified source identity:** Source-policy CI independently computed the
actual ZIP SHA512 using Python's `hashlib`, and it exactly matches the
original SourceForge `WTL10_10320_Release.zip` SHA512 already locked in
OpenATL: `086a6cf6a49a4318a8c519136ba6019ded7aa7f2c1d85f78c30b21183654537b3428a400a64fcdacba3a7a10a9ef05137b6f2119f59594da300d55f9ebfb1309`.
The GitHub mirror uses a different *filename*, but the archive bytes are
identical under SHA512. This is an in-repository relocation of the already
accepted WTL 10.0.10320 distribution, **not an upstream upgrade or fork**.
The original ZIP is kept unchanged; the 20 WTL headers, 11 official sample
projects, AppWizard and MS-PL license are preserved inside it.

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
