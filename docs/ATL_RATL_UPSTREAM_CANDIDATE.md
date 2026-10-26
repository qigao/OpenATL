# RATL upstream candidate: nested registry deletion

**Target:** `katahiromz/RATL`, pinned `2e2efa182d7c6ee9037454d23780991fb8543084` (`include/atl/statreg.h`). Candidate: `patches/ratl/0001-statreg-fail-closed-recursive-delete.patch`.

This is an ordinary, directly applicable **upstream source patch** (not a copied OpenATL component). It should remain governed by the original RATL/ReactOS ATL **LGPL-2.1-or-later** licensing and copyright notices. Apply against an unmodified checkout: `git apply --check <path>`, then `git apply <path>`. Neither upstream RATL nor a user's fork was modified automatically; the connector lacks upstream push permission and `qigao/RATL` is not present.

The patch (1) enumerates the opened child HKEY rather than the parent, (2) handles `RegEnumKeyExW` errors explicitly, (3) closes child handles on all observed failure branches and checks `RegCloseKey`, and (4) stops both `ForceRemove` and `Delete` on recursive deletion failure instead of returning S_OK or writing replacement keys. Nonexistent target keys remain allowable no-ops.

**Observed native Windows x64 evidence:** [#40347383893](https://github.com/qigao/tcc/actions/runs/40347383893) originally reproduced `S_OK` with nested stale keys for `ForceRemove` and `Delete` on untouched RATL, while the separate temporary fix removed them. A deterministic denied-deletion hook produced `0x80070005` and preserved old keys. This candidate has stricter enumeration-error propagation than that first qualified patch, so it needs its own exact-head native validation before upstream submission.

Required follow-ups: long legal subkey names, simulated concurrent writer changing a previously enumerated branch, additional error branches, and licensed RATL x64 differential CI. Test scripts remain literal HKCR-only and always use a verified GUID-named HKCU scratch hive. Arbitrary RGS scripts, real registry roots, COM activation, RATL ARM64 and product SDK inclusion remain out of scope.
