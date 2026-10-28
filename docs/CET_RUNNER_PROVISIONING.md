# Controlled x64 CET hardware acceptance runner

Status: **required infrastructure, not yet provisioned or qualified**.
See [OpenATL Issue #15](https://github.com/qigao/OpenATL/issues/15)
for hosted Windows x64 variability: both `windows-2022` and
`windows-2025` have run on workers where
IsUserCetAvailableInEnvironment(USER_CET_ENVIRONMENT_WIN32_PROCESS) returned
FALSE. Swapping image labels is not a hardware contract.

## Host admission

1. Provision a trusted Windows x64 host (physical or virtualized with actual
   CET/Shadow Stack exposed). It must have native MSVC x64 and Python
   available. GitHub Actions runner architecture must be Windows/x64.
2. Validate **actual** IsUserCetAvailableInEnvironment(0) == TRUE and
   `GetProcessMitigationPolicy(ProcessUserShadowStackPolicy)` after creating
   a CET_USER_SHADOW_STACKS_ALWAYS_ON child. Do not accept a claim based
   solely on OS version, CPU model, or PE CETCOMPAT.
3. On that host, run OpenATL's source-owned
   `tests/wtl10/cet_shadow_stack.cpp` with /guard:cf /CETCOMPAT and execute
   `--require-hardware`. Only a protected WndProcThunk call, normal
   control child, and debugged invalid-return child terminating with
   `0xC0000409` plus FAST_FAIL `0x39` qualify the machine.
4. Only **after** the checks pass, register and label the runner
   `self-hosted`, `windows`, `x64`, `openatl-cet-hsp`.
   GitHub matches all four labels, but labels are administratively
   assigned and do not automatically verify CPU or OS capabilities.
5. Use the **manual-only** workflow
   `.github/workflows/openatl-hsp-dedicated.yml` on a reviewed commit:
   MSVC and pinned llvm-mingw both compile the real CET fixture and each
   require **10 genuine native enforced executions** with exact FAST_FAIL
   subcode. On failure, the workflow remains RED; do not relabel an
   incompatible machine to force admission.

**Workflow activation boundary:** GitHub requires a `workflow_dispatch`
workflow to exist on the repository's default branch before its standard
manual-run UI is available. While this workflow exists only in Draft PR #14,
it is a reviewed **staging artifact**, not an installed or executable CI
gate. Do not claim this workflow has run or that a labeled host is connected.
After explicit authorization to add the workflow to master and after a
qualified trusted host has been registered, dispatch it on the exact source
commit and record its full MSVC/llvm-mingw native evidence. Until then,
the existing ordinary CI's mandatory HSP check remains in force.

## Security and merge boundary

- This is a **manual** workflow, not `pull_request`: do NOT run arbitrary
  fork/user-supplied PR code on a persistent self-hosted machine with
  repository credentials or secrets. Use a dedicated ephemeral/isolated
  worker for testing reviewed code, preferably rebuilt between runs.
- No secrets are needed by the OpenATL test. The workflow uses
  `contents: read`, disables persisted checkout credentials and pins the
  llvm-mingw archive by SHA256, downloading with curl, not Mono.
- This capability is OS/CPU-specific and must be checked **at runtime
  on every admission**. The label is not an evidence substitute.
- The existing regular x64 CI hardware CET test remains mandatory and
  fails closed; a controlled-runner success may only substitute for the
  hosted runner's HSP gate following explicit review of the full CI policy.
  That migration **has not been performed**.
- Do not merge Draft PR #14 while its current strict full CI matrix is RED.
  Keep Issue #15 open until controlled hardware acceptance and a verified
  exact-head integrated CI policy have been established.

References:
https://docs.github.com/en/actions/how-tos/manage-runners/self-hosted-runners/use-in-a-workflow
https://docs.github.com/en/actions/how-tos/manage-runners/self-hosted-runners/apply-labels
https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-isusercetavailableinenvironment
