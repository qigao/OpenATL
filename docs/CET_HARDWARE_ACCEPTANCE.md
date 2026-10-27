# Hardware-enforced user shadow stack qualification — x64

The OpenATL CFG/CET native scope has two distinct claims:

1. **PE image metadata:** CFG Load Config and CETCOMPAT (x64) are checked by
   verify_pe_mitigations.py. All Windows build hosts and Linux/macOS
   cross builds can qualify these image properties.
2. **Actual CPU/OS return integrity and guarded thunk:** This requires an x64 Windows process
   where ProcessUserShadowStackPolicy.EnableUserShadowStack is active,
   AuditUserShadowStack is not active; the source-owned CWndProcThunk must
   perform a true W^X indirect callback under the same enforced HSP process,
   release its page after return, and then the isolated child must produce an
   observed control-protection exception after corrupting an x64 CETCOMPAT
   function's saved return address.

The second claim is NEVER inferred from a compiler/linker flag, portable
cross-build, kernel-only HSP setting, or ARM64 runtime. No test attempts to
enable user shadow stacks at runtime when Windows has disabled them. The
feature requires hardware and OS policy and cannot be retroactively enabled
via SetProcessMitigationPolicy.

## Windows x64 native invocation

Ordinary CI builds openatl_cet_shadow_stack and runs its inventory:

    openatl_cet_shadow_stack.exe --inventory

A machine without enforced HSP prints OPENATL_CET_UNQUALIFIED with flags and
returns 0 because this **only records inventory**, not a CET success. A
supported machine runs a positive control and a separate child expected
to die from a control-protection exception. Any incorrect exit is RED.

The explicit mandatory enforcement gate is:

    openatl_cet_shadow_stack.exe --require-hardware

**Since exact-head CI #40552921431 observed actual fast-fail subcode 0x39
on BOTH native MSVC and llvm-mingw Windows x64 runners, this gate is now
REQUIRED in the ordinary pull-request workflow (not a workflow_dispatch
opt-in).** The native Windows x64 CMake invocations set
OPENATL_REQUIRE_CET_RUNTIME=ON, causing the CTest to run five times. Linux
and macOS cross-compilers only verify the PE image; ARM64 never claims
x64 CET hardware enforcement.

When HSP is unavailable or only in audit mode this command fails with
exit 77 (rather than skipping or passing). With enforcement active, a
normal child call must return normally; the tampered-return child must
terminate with STATUS_STACK_BUFFER_OVERRUN (0xC0000409) *and* the
Windows debug event's FAST_FAIL_CONTROL_INVALID_RETURN_ADDRESS subcode 0x39.
A process exit code alone is insufficient: generic stack cookie/CFG crashes
also produce 0xC0000409. The parent uses DEBUG_ONLY_THIS_PROCESS solely
for the isolated corrupted-return child and preserves the exception record.

Build through normal manifest-installed WTL/OpenATL source-tree CMake with:

    -DOPENATL_BUILD_QUALIFICATION=ON -DOPENATL_REQUIRE_CET_RUNTIME=ON

That option adds a required native CTest only for x64 and fails CMake on
ARM64. The ordinary PR workflow and workflow_dispatch both require this on
native Windows x64 MSVC and llvm-mingw; no opt-out mode exists in CI. A
runner without Windows user HSP enabled will be RED, as required. Dedicated hardware-backed Windows
execution remains an acceptance dependency, not a simulated provider.

Only a spawned child process intentionally corrupts its return address.
The parent validates child exit with a 7-second timeout and releases its
handles. No in-process unsafe return corruption, no security mitigation
downgrades, and no COM Server/Installed SDK/provider fallback.

## Source and provenance

The approach follows the native Windows _AddressOfReturnAddress() intrinsic
documented by Microsoft, and the isolated CET death-test pattern in Chromium
(cet_shadow_stack_unittest.cc). No Chromium or Microsoft implementation is
copied into OpenATL. The test checks the behavior of OpenATL's executable
under the host policy and does not certify all Windows applications.

References:
- https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-process_mitigation_user_shadow_stack_policy
- https://learn.microsoft.com/en-us/cpp/intrinsics/addressofreturnaddress
- https://chromium.googlesource.com/chromium/src/+/lkgr/base/win/cet_shadow_stack_unittest.cc
