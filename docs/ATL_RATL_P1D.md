# RATL P1-D: nested registry script differential

Track evidence in [Issue #9](https://github.com/qigao/tcc/issues/9).

Two **independent Windows x64 PE** programs compile exactly the same
neutral contract fixture. One uses the unmodified RATL
`2e2efa182d7c6ee9037454d23780991fb8543084`; the other uses a
separate *temporary* copy of that precise commit with a strictly
anchored test-only `RegDeleteTreeX` correction. Neither copy is linked
to OpenATL nor redistributed inside an OpenATL artifact.

All fixed scripts are **HKCR-only**. A fresh GUID-named HKCU test key is
first created and a marker proves process-local HKCR redirection before
the script executes. Never pass external scripts: upstream RATL also
recognizes HKLM and HKCU roots.

Each platform verifies nested register/unregister, REG_SZ, REG_EXPAND_SZ,
REG_DWORD and REG_BINARY values, and `NoRemove`. Existing
`ForceRemove` and `Delete` trees have nested descendants. The unmodified
RATL should expose their survival, demonstrating the original
parent-versus-child enumeration defect; the separately patched RATL
should remove those descendants. A baseline that unexpectedly succeeds,
or a patched case that still leaves stale keys, MUST fail CI rather than
silently change golden expectations.

Windows x64 executes 5 repetitions under a per-subprocess timeout, while
Linux and macOS independently cross-compile both PE variants. The
original RATL source remains unchanged and is never mixed with OpenATL.
No RATL ARM64 or production ATL deployment is qualified; no WTL8
fallback or Salts production change is authorized.

The expected native diagnostics now include the exact upstream HRESULT
(`0x00000000`, i.e. `S_OK` despite stale descendants) and actual
nested-tree state: `stale=1` for unchanged RATL and `stale=0` for the
separate test-only fix. The test first observes real keys and asserts the
result before printing these lines. Both baselines require a clean Git
checkout of the source pin; accidental mutation is a hard error.

### Deterministic deletion-denied conformance

A **third, separately named** Windows x64 PE is built using the exact-pinned
test-only patched RATL checkout and a compile-time test fault flag
(`RATL_P1D_TEST_DENY_DELETE=1`). The only affected key name is the
literal `RATL_P1D_DENIED` under an authenticated disposable HKCU/HKCR
test scope; the test does not modify real registry ACLs.

The test-only patch now also checks and propagates `RegDeleteTreeX` errors
from `ForceRemove` and `Delete` call sites. Both injected modes
must return `HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED) = 0x80070005`,
preserve the old nested branch, and refuse the new value. Any `S_OK`,
overwritten record, missing old branch, or cleanup failure rejects CI.
All native x64 tests run five times; Linux/macOS cross-compile a separate
injected PE. This is NOT production qualification for arbitrary ACL
failures or concurrent registry mutation.

### Artifact and licensing separation

The Linux/macOS CI still **requires** genuine PE compilation for original,
test-patched and injected RATL reference programs. Those executables may
contain RATL LGPL/GPL header implementation and **must not be packaged as
WTL/OpenATL product/SDK artifacts**. The cross-host upload action explicitly
excludes `atl_com_ratl_*.exe`, `atl_com_p1_ratl_*.exe`,
`atl_ratl_registry_ref_*.exe` and `ratl_nested_*.exe` while retaining
required CI logs and OpenATL/WTL10 PE evidence. The Windows ARM64 artifact
already contains only OpenATL/WTL10 binaries. This does not replace a
separate license assessment if anyone later distributes RATL test binaries.

### File-specific license notice

The test-only Python patch `scripts/ratl-rgs-nested-test-patch.py`
contains short excerpts from pinned ReactOS ATL `statreg.h`,
Copyright 2005 Jacek Caban and 2009 Andrew Hill, licensed
**LGPL-2.1-or-later**. This file now carries a file-specific SPDX
identifier and upstream attribution, separate from MIT OpenATL.
The patched temporary RATL checkout retains its original licensing.
See https://github.com/katahiromz/RATL/blob/2e2efa182d7c6ee9037454d23780991fb8543084/include/atl/statreg.h
and https://github.com/katahiromz/RATL/blob/2e2efa182d7c6ee9037454d23780991fb8543084/COPYING.LIB.
No RATL implementation has been vendored into the OpenATL headers or
released in WTL10 SDK artifacts.
