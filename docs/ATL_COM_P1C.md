# P1-C: scoped Windows registry sandbox preflight

See [Issue #8](https://github.com/qigao/tcc/issues/8). This is a
**precondition test**, not a working OpenATL `DllRegisterServer`.

The native test runs in a fresh Windows x64/ARM64 process. It creates a
new GUID-scoped key directly under `HKEY_CURRENT_USER`; no elevation
or machine registry modification is needed.

1. Calls `RegOverridePredefKey(HKEY_CLASSES_ROOT, key)`.
2. Writes a marker **through the temporary HKCU handle only** and
   proves it is readable via HKCR before any HKCR write.
3. Confirms that unimplemented OpenATL `RegisterServer` and
   `UnregisterServer` fail closed with `E_NOTIMPL`.
4. Exercises explicit redirected registry writes as a sandbox self-test,
   verifies bytes inside HKCU, rolls back the temporary class tree and
   verifies its absence. No actual class server is installed.
5. Restores the HKCR mapping **before** deleting the temporary root,
   then confirms the HKCU scratch key no longer exists.

The test must fail if isolation, redirection, restoration, or cleanup
cannot be verified. A destructor also attempts cleanup on early failure.
Test executable is native x64 and native ARM64; Linux/macOS build PE
only. This does not qualify actual persistent COM registration,
RGS parsing, crash recovery, admin/policy behavior, full COM server,
or production safety.

Microsoft documentation explicitly states that `RegOverridePredefKey`
only affects the calling process:
https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regoverridepredefkey

The upstream MIT OpenATL source is unchanged. RATL LGPL/GPL code is
not used. PR #1 stays Draft / DO NOT MERGE.

### Required controlled abrupt-exit recovery gate

`registry_real.cpp` now runs a **parent / aborted child / independent
recovery child** sequence after its scoped registrar tests. The parent
creates a fresh GUID-named HKCU root and sets a GUID-derived proof, and
only the child receiving the matching scoped environment may access it.

The first child proves remapping and registers two distinct test CLSIDs
using the test-only OpenATL API, flushes the sandbox hive, then intentionally
exits through `TerminateProcess(..., 77)` without C++ cleanup. The parent
checks both persisted keys under the *direct HKCU handle* and closes its
handle before starting a **second fresh process**. That recovery child
validates the GUID shape and proof, confirms the two records, removes the
sandbox root and returns 0. The parent independently verifies its absence.

Each platform requires five normal+recovery executions, no manual clearing,
and exact expected stdout. This validates a controlled process-abort
scenario, **not** power-loss durability, arbitrary crash recovery,
production DllRegisterServer or external service activation. No HKCR/HKLM
persistent writes are authorized.

### Independent RATL statreg registry-script reference

tests/atl_conformance/ratl_registry_reference.cpp compiles as a separate
Windows x64 executable against exact-pinned, unchanged RATL. A constant
RGS script uses only HKCR root plus REG_SZ/REG_DWORD; it executes through
RATL CRegObject, after verifying process-local HKCR redirection to a newly
created disposable HKCU root. Five x64 native runs require RGS register,
value inspection, unregister disappearance, invalid-script failure/rollback,
and full scratch cleanup. Linux/macOS also cross-compile that exact code.

RATL accepts other roots such as HKLM, so arbitrary scripts must never
be executed inside this constrained reference. RATL remains x64-only:
its ARM64 PSEH3 is not qualified. No RATL headers, source, object layout,
or ownership are mixed with OpenATL. This comparison does not authorize
real COM product installation or arbitrary RGS features.
