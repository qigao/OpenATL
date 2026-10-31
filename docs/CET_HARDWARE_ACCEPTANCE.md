# Optional local x64 hardware HSP diagnostics

By owner decision (2026-10-31), actual hardware HSP enforcement is outside CI,
including manually dispatched GitHub Actions workflows. It is not a merge or
feature-completion gate. No dedicated runner is required.

CI continues to check CFG/CETCOMPAT PE metadata on the normal mitigation binary,
plus native CFG/W^X and dynamic-code-denial behavior. Those checks do not claim
hardware return-address enforcement.

## Manual use

The existing x64 diagnostic target is EXCLUDE_FROM_ALL, has no CTest entry,
and is built only when explicitly requested on a Windows x64 host:

```powershell
cmake --preset msvc-x64
cmake --build --preset msvc-x64 --target openatl_cet_shadow_stack
.\build-msvc-x64\openatl_cet_shadow_stack.exe --inventory
.\build-msvc-x64\openatl_cet_shadow_stack.exe --require-hardware
```

The llvm-x86_64 preset can build the same explicit target. ARM64 has no x64
hardware HSP diagnostic target. The former OPENATL_REQUIRE_CET_RUNTIME CTest
option and both hardware-only workflows are removed.

Both commands are manual diagnostics. Inventory may itself execute a hardware
death test on a protected host, so it is also excluded from CI. The explicit
require-hardware mode creates a child with mandatory HSP, checks an actual
source-owned RX thunk, and requires a debug event with FAST_FAIL 0x39 after an
invalid return. Unsupported hardware/OS policy fails that manual invocation;
it does not affect ordinary builds, CI or completion status.

No production protection or thunk implementation is removed. A CETCOMPAT flag
alone remains image metadata, not proof of CPU/OS enforcement.

References:
- https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-process_mitigation_user_shadow_stack_policy
- https://learn.microsoft.com/en-us/cpp/intrinsics/addressofreturnaddress
