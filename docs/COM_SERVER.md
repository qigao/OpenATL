# ATL COM Server — implementation and compatibility roadmap

COM Server and COM Client are both mandatory OpenATL capabilities. The
existing source-owned server core under `OpenATL::COM` complements
`atlcom.h` and provides
nonaggregated COM objects, explicit interface/class tables, class factories,
module lifetime accounting, and explicit registry registration. Standard ATL
DLL module exports and `OBJECT_ENTRY_AUTO` class discovery exist. A class
can now supply its own real `UpdateRegistry(BOOL)` callback to participate
in deterministic automatic DLL registration; the callback is not inferred
from the CLSID. RGS resources, type-library registration, aggregation and
the full ATL COM Server API remain compatibility gaps.

For conventional ATL object declarations, `atlcom.h` supplies the
[nonaggregated object and COM-map compatibility subset](ATL_COMPATIBILITY.md).
Its module-aware creator plugs into the same explicit class table. Local
ATL-style creation retains zero-ref semantics and pins an active application
CAtlModule if present; DLL classes still use the explicit COM::Module-aware
creator. The [modern EXE module](ATL_MODULE.md) separately owns its COM startup
and explicit process-local class registration.

## Ownership and concurrency protocol

- The DLL owns one `Module` and an immutable `ClassEntry` table. Both remain
  alive until all factories, objects, server locks and external DLL callers
  have retired. Neither table mutation nor module destruction is concurrent.
- Every factory and concrete object owns one `ModuleReference`. Interface
  pointers own object references via `QueryInterface`/`AddRef`/`Release`.
  The last release destroys the derived object before releasing its module
  reference. Constructor failure rolls back that reference.
- Each successful `LockServer(TRUE)` adds a separately balanced module
  reference. Unbalanced unlock returns `E_UNEXPECTED`; counter exhaustion
  fails acquisition. Misuse of a released interface is outside the contract.
- Module and interface reference counters support multiple concurrent callers.
  This does not synchronize application object fields or marshal interfaces.
  A `Both` class must implement its own data synchronization, and clients
  must marshal pointers when crossing apartment boundaries.
- `CanUnloadNow()` returns `S_OK` only with no retained factory, object or
  server lock. It does not authorize forced `FreeLibrary` during an export,
  callback, worker, or an external DLL call. The host must obey COM unload
  rules and retire its own external callers.
- `GetClassObject` and `CreateInstance` clear valid output pointers before
  failure. Unknown CLSIDs/IIDs fail explicitly; aggregation is rejected.
  Derived constructors may throw; creation translates allocation failure to
  `E_OUTOFMEMORY` and other C++ exceptions to `E_UNEXPECTED`.
- The server never initializes COM, including in `DllMain`. The client owns
  apartment initialization. DLL entry points forward to the module and
  explicit registration API; they are ordinary application exports.

## Registration boundary

Registration accepts an explicit classes-root handle, one CLSID, optional
ProgID, description, absolute DLL path and threading model. The caller owns
the root handle and coordinates registration/unregistration exclusively with
installers and other registry writers. The process-default registry view is
selected by that handle; there is no hidden HKLM or elevation fallback.
Current-user COM deployment uses `HKCU\Software\Classes` explicitly.
ProgIDs are optional, start with an ASCII letter, and contain at most 39
ASCII letters, digits or periods. DLL paths are absolute and bounded to
32,000 UTF-16 code units; descriptions are bounded to 4,096 code units.

Registration creates new class/ProgID keys only and refuses existing keys.
It records ownership and rolls back its new entries if a subsequent step
fails. Unregistration validates ownership and expected values/children before
deleting known entries; unexpected or modified content is preserved and
reported as a conflict. No recursive deletion or general RGS execution occurs.
Registry operations are not a transaction against concurrent external writers;
cleanup failure is returned and may require installer intervention.

## Multi-class registration milestone

`RegisterClasses(root, entries, count)` and
`UnregisterClasses(root, entries, count)` extend the existing single-class
registrar to a bounded, explicit table of up to 1,024 COM class descriptors.
They share the **same** `RegisterClass`/`UnregisterClass` owner markers,
CLSID/ProgID registry plan, safe enumeration and nonrecursive deletion;
no second registry engine, COM apartment, elevation or fallback is introduced.

The entire table is validated **before registry mutation**, including each
module path, threading model, CLSID and case-insensitively unique ProgID.
If a later class registration fails, the API attempts reverse-order rollback
of only the classes installed by this batch. A rollback error is returned
instead of hidden. Batch unregistration checks every expected owner/value
and child key before removing *any* class, so a pre-existing foreign value
in a later entry does not partially remove earlier entries.

A batch call is **not an atomic Win32 registry transaction**. The caller must
serialize its installer writes against other processes and handle partial
registry cleanup after OS failures. Neither method infers a ProgID or
registration metadata from `OBJECT_ENTRY_AUTO` alone: that macro currently
selects class factories, not RGS/Windows installation semantics. The
standard ATL class-directed registrar and resource scripts remain upcoming
compatibility milestones.

## Class-directed ATL `UpdateRegistry` milestone

A class declared with `OBJECT_ENTRY_AUTO(clsid, Class)` may implement
`static HRESULT WINAPI UpdateRegistry(BOOL install)` using its actual
CLSID, ProgID, path, registry root and threading model. The header stores
a non-owning typed callback in each static object-map node, alongside the
factory creator. The DLL module invokes the callbacks only on explicit
`RegisterServer`/`UnregisterServer` requests; no registry operation
happens in `DllMain` or during simple factory lookup.

Entries are sorted deterministically by CLSID after a bounded validation
pass. All selected class callbacks must exist before any registry mutation;
otherwise registration fails with `E_NOTIMPL`. Successful calls register
in that order. If a later class fails, the DLL module invokes earlier
completed registrations' inverse callbacks in reverse order. Unregistration
also uses reverse order. A derived DLL module may supply its own registrar.
An explicitly bound manual factory table remains supported and does not
silently borrow unrelated auto-map registration callbacks.

For intentional classes requiring no Windows registration,
`DECLARE_NO_REGISTRY()` implements the deliberate `UpdateRegistry` no-op.
Without such an explicit decision, absent callbacks are **errors**.

The formal DLL fixture registers two distinct classes to
`HKCU\\Software\\Classes`, tests actual STA/MTA `CoCreateInstance`,
and injects a foreign second ProgID to prove first-class rollback.
All installation metadata remains class/application owned, with no implicit
HKLM registration or elevation. Callback-based rollback cannot prevent
concurrent external registry writes; use the strict owner-verified
`RegisterClasses`/`UnregisterClasses` APIs when a shared descriptor
batch and complete preflight are needed.

**Still missing:** resource-driven `DECLARE_REGISTRY_RESOURCEID`/RGS execution,
ATL's complete class registry/type-library APIs, and broad Microsoft ATL
sample compatibility. The callbacks are not an RGS interpreter.

## Integration and validation

Link the single `OpenATL::ATL` target. It supplies `ole32`, `oleaut32`,
`uuid` and `advapi32`; no separate client/server/registration targets are
required. Include `openatl/com_server.h` or `openatl/com_registration.h`
explicitly. Neither linking nor including a header initializes COM or
registers a class.

For a user-defined `IExample : IUnknown`, use an explicit IID descriptor:

```cpp
using namespace OpenATL::COM;
class Example final : public Object<Example, Interface<IExample, &IID_IExample>> {
public:
    explicit Example(Module& module) : Object(module) {}
    // Implement all IExample methods; methods must respect COM error/ABI rules.
};
const ClassEntry classes[] = {{CLSID_Example, &Example::CreateInstance}};
Module module(classes, 1);
```

The interface descriptors must use distinct IIDs; the first interface is the
canonical `IUnknown` identity. An inherited base IID is exposed with
`InterfaceAlias<IBase, &IID_IBase, IDerived>` alongside the implemented
`Interface<IDerived, &IID_IDerived>`; this reuses the same subobject.
A custom `Creator` must obey COM output/refcount
rules and retain the module for its concrete object's entire lifetime. The
provided `Object` template does that automatically. The concrete destructor
must be accessible to `Object` and must not throw.

`DllGetClassObject` forwards to `module.GetClassObject`, and `DllCanUnloadNow`
forwards to `module.CanUnloadNow`. Export them through a `.def` file. Registration
is separate: `RegisterClass(root, entry)` and `UnregisterClass(root, entry)` use
the same `ClassRegistration` description and return `HRESULT`. A new
registration conflict returns `HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)`;
ownership/content conflicts during removal return access denied. Missing
owned keys are tolerated during removal. When rollback fails, its error is
returned in preference to the initial write error, and remaining owned keys
may need cleanup. Successful registration requires exclusive installer access;
it does not promise atomicity across multiple class registrations.

The complete compilable DLL/export and client examples are the formal
`tests/com_server/fixture.cpp` and `tests/com_server/server.cpp` fixtures.

Formal native tests cover interface identity, creation failures, concurrent
reference accounting, module/server locks, a real server DLL, STA/MTA COM
activation, registry conflicts, rollback, foreign-content preservation and
unregistration. Windows x64/ARM64 use the same source with MSVC and LLVM-MinGW;
cross builds establish PE compilation only.

References:
- https://learn.microsoft.com/en-us/windows/win32/api/unknwnbase/nn-unknwnbase-iclassfactory
- https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-dllcanunloadnow
- https://learn.microsoft.com/en-us/windows/win32/com/in-process-server-threading-issues
