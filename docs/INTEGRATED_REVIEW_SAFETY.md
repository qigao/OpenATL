# Combined native WTL security and ATL source ownership acceptance

This exact tree retains the four original validation branches of OpenATL:
SDI interactive WTL10 (PR #7), native CFG/hardware x64 CET (PR #8),
bounded native DLL unload (PR #11), and post-review ATL public-header
ownership fixes (PR #13). The source-owned headers from PR #13 replace
their unchanged master versions in the already qualified PR #12 tree.
No fallback, legacy SDK, COM Server, install packaging or provider has
been added.

Exact-head CI must run all tests together on one binary set:
- Real Windows x64 MSVC and llvm-mingw full CTest repeated five times,
  including CFG/CET FAST_FAIL subcode 0x39 on the actual WndProc thunk.
- Real Windows ARM64 MSVC and llvm-mingw, SHA256+PE verified before
  executing the native fixtures and genuine SDI dialogs.
- DLL owner and caller-lease shutdown/reload regression with live blocked
  callbacks, and real Unicode SDI edit forwarding and menu/dialog actions.
- Source-owned ATL object lifetime tests, deep copies/throws, bounded
  temporary buffers, and safe window/dialog early-failure cleanup.
- Linux/macOS Windows x64/ARM64 PE cross CMake and source-only policy.

No claim of support for arbitrary uncooperative DLL hot-unload, non-x64
CET, COM servers, or a production Installed SDK. Keep this PR Draft until
its own full GitHub Actions matrix passes on this exact head. Parent PRs
remain open and unmerged until a final merge decision.

Strict Windows ACP conversion now rejects unmappable/default-substituted
Unicode and malformed byte sequences; CTempBuffer uses matched C++17
aligned new/delete for over-aligned heap types and tests zero/negative
fixed-buffer capacity. The Windows CET gate explicitly requests hardware
HSP at child creation; its actual process policy and FAST_FAIL=0x39
remain mandatory, independent of the parent runner's default HSP state.
See Issue #15 for hosted-runner variability.
