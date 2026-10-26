# ATL COM P1 — factory and fail-closed registry qualification

Tracked in [Issue #7](https://github.com/qigao/tcc/issues/7).

This is a **separate native OpenATL P1 qualification**, building on the
qualified, isolated P0 COM identity comparison in Issue #6. No RATL
license-bearing code is included; RATL x64 remains an independent P0
reference and RATL ARM64 remains unqualified.

- Source pin: OpenATL `7b587f3e10f3f8d93a01522033de80007368bad5`.
- Temporary-checkout-only `scripts/openatl-com-p1-hardening.py` fails
  closed unless the exact source matches. It changes the unimplemented
  `CAtlModuleT::RegisterServer/UnregisterServer` methods to
  `E_NOTIMPL`, adds creator-side non-aggregation rejection, and checks
  missing class-factory callbacks instead of dereferencing NULL.
  **Upstream OpenATL is not modified**.
- `tests/atl_conformance/com_p1.cpp` checks one-time construction and
  destruction, `FinalConstruct` failure rollback, reference balances,
  object/factory `QueryInterface`, factory null/unsupported/aggregation
  failures and module LockServer accounting.
- `CoRegisterClassObject` / `CoGetClassObject` / `CoRevokeClassObject`
  provide a **process-local** transient activation check. These never
  register permanent classes or write to HKCR.
- Repeated native Windows x64/ARM64 execution and matching PE+SHA256
  are required. Linux/macOS cross-compilation is supplementary evidence.

**Not yet qualified:** persistent COM registry operations (deliberately
E_NOTIMPL), registry hive rollback, DllRegisterServer, complete COM
server/object maps, `IDispatch`, aggregation implementation, STA/MTA,
marshalling, ActiveX/GUI production. P1 only covers explicit gates.
PR #1 stays Draft/DO NOT MERGE; no Salts production switch.


### Independent RATL P1 x64 reference

A separate, authored neutral-contract fixture `tests/atl_conformance/ratl_p1_reference.cpp` exercises comparable `CComObject`, `FinalConstruct` failure rollback, `IClassFactory`, `LockServer`, temporary `CoRegisterClassObject`/`CoRevokeClassObject` and nonaggregation behavior. It is compiled **only** with the pinned RATL header tree and run as a separate x64 process, never linked to OpenATL. RATL's uninitialized class-factory callback and persistent registry APIs are explicitly **not** invoked. RATL Windows ARM64 remains unqualified (PSEH3 x86-only); separate x64 reference results do not establish full Microsoft ATL semantic parity.
