# Integrated OpenWTL native security qualification

The current exact-head CI combines:

- Windows x64 MSVC/llvm-mingw native UI/COM/WTL suites, repeated five times.
- CFG/CETCOMPAT PE metadata, real guarded RX thunks and dynamic-code denial.
- DLL unload/reload with closed admission, bounded caller leases, joined callers,
  thunk retirement, window-class unregister and module ownership.
- SHA256/PE/resource-verified real Windows ARM64 native behavior on both compilers.
- Source-owned ATL/WTL provenance and no implicit COM initialization for UI.

Actual hardware HSP enforcement and real printer probes are outside CI by owner
decision (2026-10-31). There are no hardware-only workflows or required dedicated
runners. Device-independent printing COM callbacks remain covered. Optional
local diagnostics do not block merge or feature completion and do not turn PE
metadata into a hardware-enforcement claim.

See [ACCEPTANCE.md](ACCEPTANCE.md), [TOOLCHAIN_MATRIX.md](TOOLCHAIN_MATRIX.md),
[UNLOAD_ACCEPTANCE.md](UNLOAD_ACCEPTANCE.md) and
[CET_HARDWARE_ACCEPTANCE.md](CET_HARDWARE_ACCEPTANCE.md).
