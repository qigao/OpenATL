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

The hosted LLVM-MinGW CI job builds openatl_cet_shadow_stack and verifies
its CFG/CETCOMPAT image metadata, but executes no HSP runtime tests. It sets
OPENATL_REQUIRE_CET_RUNTIME=OFF, excludes openatl.cet_* from CTest, and does
not invoke --inventory directly. GitHub-hosted workers do not guarantee
user shadow-stack availability (Issue #15). Inventory can itself execute
the hardware death test when the parent process already has HSP enabled.

The MSVC job and manual hardware workflows retain inventory:

    openatl_cet_shadow_stack.exe --inventory

A machine without enforced HSP prints OPENATL_CET_UNQUALIFIED with flags and
returns 0 because this **only records inventory**, not a CET success. A
supported machine runs a positive control and a separate child expected
to die from a control-protection exception. Any incorrect exit is RED.

The explicit mandatory enforcement gate is:

    openatl_cet_shadow_stack.exe --require-hardware

The ordinary hosted MSVC x64 job now sets
OPENATL_REQUIRE_CET_RUNTIME=OFF because generic windows-2022 workers do
not guarantee hardware/user Shadow Stack (issue #15). This hosted run
checks CFG/CET PE metadata, native UI/COM and the HSP inventory, but
explicitly **does not qualify hardware HSP**. The strict mandatory
enforcement gate remains available locally and through the dedicated
openatl-hsp-dedicated.yml workflow, where MSVC and llvm-mingw each execute
ten real enforced HSP tests on a qualified host. A passing generic hosted
job does not establish hardware HSP enforcement. Linux/macOS cross-builds
only verify PE metadata; ARM64 never claims x64 CET hardware enforcement.

When HSP is unavailable or only in audit mode this command fails with
exit 77 (rather than skipping or passing). With enforcement active, a
normal child call must return normally; the tampered-return child must
terminate with STATUS_STACK_BUFFER_OVERRUN (0xC0000409) *and* the
Windows debug event's FAST_FAIL_CONTROL_INVALID_RETURN_ADDRESS subcode 0x39.
A process exit code alone is insufficient: generic stack cookie/CFG crashes
also produce 0xC0000409. The parent uses DEBUG_ONLY_THIS_PROCESS solely
for the isolated corrupted-return child and preserves the exception record.

Build through the source-tree `msvc-x64` preset (which enables the required
hardware test), or explicitly select `OPENATL_REQUIRE_CET_RUNTIME=ON` in the
`llvm-x86_64` preset on a qualified hardware host. CMake manages WTL itself;
no ports installation is needed.

That option adds a required native CTest only for x64 and fails CMake on
ARM64. Wherever the hardware gate is explicitly enabled, missing user HSP
still fails the test. The hosted LLVM-MinGW exception changes workflow
selection only; it does not weaken the hardware test or its FAST_FAIL proof.

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

## Creation-time HSP admission (native Windows x64 only)

The --require-hardware command no longer assumes that its parent runner
process has user Shadow Stack enabled by default. It creates a new
CETCOMPAT process with STARTUPINFOEXW, UpdateProcThreadAttribute and
PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY. The second 64-bit creation-policy
word contains CET_USER_SHADOW_STACKS_ALWAYS_ON (1ULL << 28). All subsequent
control and debugged invalid-return children use this same mandatory policy.

The spawned protected child must confirm an enabled, non-audit user HSP
policy, execute and retire a genuine source-owned RX WndProcThunk, then
record a native FAST_FAIL_CONTROL_INVALID_RETURN_ADDRESS (0x39) from a
debugger event. A failed CreateProcess or unsupported hardware/OS policy
is RED. No always-off, silent skip or weak verification path is provided.
The --inventory mode still reports the default *parent* process policy
and does not qualify HSP on its own. GitHub-host variability is tracked
in Issue #15.

API: https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-updateprocthreadattribute
