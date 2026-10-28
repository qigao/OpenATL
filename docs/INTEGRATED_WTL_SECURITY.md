# Integrated OpenATL WTL security native qualification

This Draft composes the exact HEADs of OpenATL PR #7 (SDI UI), #8 (CFG /
hardware x64 CET) and #11 (bounded DLL unload). The merge tree has all
three heads as parents. No release, Installed SDK, COM Server, provider
fallback, runtime switch or compatibility API is added.

**ONE exact-head CI must simultaneously require:**
- Actual WTL10 SDI + comctl32 v6 manifest: native menus, modal dialogs,
  button, remote Unicode WM_SETTEXT -> EN_CHANGE, and File/Exit.
- Real Windows x64 MSVC and llvm-mingw CFG guard PE, hardware CET Shadow
  Stack with actual WndProc thunk, and separate corrupt-return child with
  precise FAST_FAIL=0x39. The current hosted LLVM-MinGW job checks image
  metadata and excludes HSP runtime tests because worker capability varies;
  LLVM-MinGW hardware enforcement is qualified locally or by the dedicated
  manual workflow. MSVC retains five mandatory native HSP repetitions.
  ARM64 never claims x64 CET compatibility.
- DLL plugin: real LoadLibrary/FreeLibrary 12 rounds per native test,
  deterministically held callback, closed submission admission, bounded
  external caller lease (including capacity/underflow), joined producers,
  WNDPROC/thunk retirement, class unregister and reload; five repeats.
- SHA256+PE verified genuine Windows ARM64 native MSVC/llvm-mingw, and
  Linux/macOS x64/ARM64 Windows cross-build validation.

These sources modify the same CMake, CI workflow and native ARM64 manifest;
individually green PRs do not establish combined CI acceptance. Keep all
Draft PRs unmerged until this precise combined head passes and final
source review sets the merge sequence.
