# Vendored WTL 10.0.10320

This directory contains the complete WTL 10.0.10320 release tree (Include, all 11 Samples, AppWizard, ReadMe.html, and MS-PL.txt), not an ATL implementation or a second runtime. **Microsoft Public License (MS-PL)** governs the WTL sources and assets; keep the original headers and license notices.

- Release archive SHA-512 (the original vcpkg-cache/ports/wtl source contract): `086a6cf6a49a4318a8c519136ba6019ded7aa7f2c1d85f78c30b21183654537b3428a400a64fcdacba3a7a10a9ef05137b6f2119f59594da300d55f9ebfb1309`.
- Git-import provenance: `dnobori/DN-Win32DiskImagerRenewal@a449c415dbd2484153aabc72d78729a9bd9fa720`, `WTL10_10320_Release/`. The manifest inventories all 296 byte-level Git blobs of the *checked-in* source, so binary resources and legacy code-page sources are not recoded.
- Exactly seven pre-existing vcpkg-cache compatibility patches from `qigao/vcpkg-cache@ddb500fb7c1472107efbd79ea92d7552cb01f452` are applied in-tree to the six paths listed in `source-manifest.json`. No new API-deletion adapter or second WTL provider.
- The original `atlbase.h`, `atlwin.h` and COM Client/Server implementation remain repository-owned under `../alt/include/`; WTL is the GUI front end, linked through `OpenWTL::WTL` to the private `openatl_atl` backend.

The release archive digest documents the selected upstream version; the Git-blob manifest independently proves the checked-in tree. Do not claim that a mirror Git commit alone independently re-verifies the archive SHA-512. For upstream updates, change this manifest and the exact source version in the same reviewed PR and retain all original native tests.
