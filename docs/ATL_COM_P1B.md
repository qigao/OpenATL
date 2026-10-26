# OpenATL P1-B — bounded OBJECT_ENTRY_AUTO experiment

Tracked by [qigao/tcc Issue #7](https://github.com/qigao/tcc/issues/7).
This is a **test-only patch applied to temporary checkouts** of the exact
MIT OpenATL commit `7b587f3e10f3f8d93a01522033de80007368bad5`;
upstream remains unchanged, and RATL LGPL/GPL code is **not copied**.

- Build two independently registered `OBJECT_ENTRY_AUTO` objects in
  **different translation units**; reject missing/duplicate/overflowed
  registrations (maximum 16 POC entries). Fail closed on unsupported IDs.
- Module-managed `GetClassObject` and
  `RegisterClassObjects`/`RevokeClassObjects` operate on a bounded
  in-memory object map and *real* Windows COM class object table.
  Static factories stay alive until process exit, without heap allocations
  crossing ATL providers. No persistent registry is written.
- Inject failure after registering the first entry and require complete
  rollback; check cookies and that neither object remains activated. Repeat
  normal registration/revocation, validate both classes, duplicate
  registration rejection, post-revoke absence and wrong-thread fail closed.
- CI builds Windows x64/ARM64, executes 5 native iterations on Windows,
  and separately cross-builds on Linux and macOS, with pinned llvm-mingw
  and PE SHA256 validation. Never count compilation as runtime evidence.
- `CAtlModuleT::RegisterServer/UnregisterServer` remain `E_NOTIMPL`.
  **Actual persistent registry, RGS parsing, DllRegisterServer, secure
  private hive and failure recovery are not implemented**. This test does
  not qualify a production COM server or Microsoft ATL parity.

Negative admission paths are required on *native* Windows x64/ARM64 in **separate processes**: malformed null map entry, duplicate CLSID, and the 17th entry in a 16-entry bounded map. After any rejection, the persistent process-local error must block both class lookup and class registration; successful normal-test runs must not share state with these poisoned-mode runs.
