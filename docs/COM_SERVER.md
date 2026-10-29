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

**Further work:** the `DECLARE_REGISTRY_RESOURCEID(id)` path now loads the
actual Windows `REGISTRY` resource and delegates to the existing bounded
RGS parser/owner-verified executor through a scoped application policy.
Full ATL RGS variants, automatic installation/elevation, complete TypeLib
registration, and broad unchanged Microsoft ATL project compatibility
remain unqualified.

## RGS grammar admission — read-only, source-owned

The new `openatl/rgs_parser.h` implements a **bounded C++17 parser and
read-only projection** for an initial subset of real ATL registration
resources. It accepts a symbolic `HKCR` root; nested `NoRemove` and
`ForceRemove` keys; case-insensitive key/value names; default and named
`REG_SZ` values (`= s '...'` and `val Name = s '...'`);
GUID key names; quoted bare `Programmable` keys; `TypeLib`,
`Version`, `AppID`, `LocalizedString`; and explicit, bounded
`%MODULE%` replacement.

Inputs must fit **65,536 UTF-16 code units**, **512 keys**, **1,024 values**,
**20 nested scopes** and bounded name/path/value lengths. Unsupported
roots, value types or substitutions, malformed quotes/braces, case-folded
duplicate keys/values, traversal names and ownership-marker collisions
fail closed. Failure leaves the output document unchanged.
`ResolveModule` produces an independent expanded document and never reads
environment variables or the registry.

**The parser alone never executes registry instructions.** `ForceRemove`
and `NoRemove` are AST directives until an application separately calls
the opt-in `RegisterRgs` / `UnregisterRgs` methods with an opened
caller-owned Classes root. The parser never opens HKCR/HKLM,
recursively deletes keys, loads RGS DLL resources or initializes COM,
and resource-driven `DECLARE_REGISTRY_RESOURCEID` is supported only for the
bounded source-owned RGS subset under explicit installer policy, not arbitrary
`CAtlRegistrar` compatibility. Subsequent wider registrar work must
accept an **explicit caller-owned root** and reject foreign-key/value
changes rather than treating `ForceRemove` as unqualified authorization
to delete another program's data.

The binary-native COM core test parses **four byte-identical Microsoft
VCSamples RGS resources** (pinned revision
`9e1d4475555b76a17a3568369867f1d7b6cc6126`): COMMap Chain/Outer,
ATLSafeArray ArrayManager and ATLCollections MapCollection. Their original
Git blob identities are checked at CMake configure time, their node/value
counts and values are compared, and malformed inputs are tested
independently. A successful parse is **not** evidence that those original
Microsoft ATL projects build, register or activate against OpenATL.

## Owner-verified RGS execution — explicit HKCU/Classes root

`openatl/rgs_registration.h` now provides
`OpenATL::COM::RegisterRgs(HKEY classesRoot, const RGS::Document&, modulePath, ownerTag)`
and `UnregisterRgs(...)`, reusing the existing
`registration_detail::Plan`, `inspect` and `remove` implementation rather
than maintaining a second COM/registration provider. The parser itself
(`rgs_parser.h`) remains read-only. A symbolic source `HKCR` does **not**
select a Windows root: callers must supply an **opened, writable** Classes
key. Predefined `HKEY_CLASSES_ROOT`, `HKEY_LOCAL_MACHINE`,
`HKEY_CURRENT_USER` and other predefined handles are rejected, and there
is no implicit elevation or registry-view switch.

Registration requires an absolute, bounded, canonicalized DLL path and a
caller-owned ASCII registration tag (1–64 letters/digits/periods/hyphens/
underscores). The entire resolved RGS tree is validated **before mutation**:
bounded depth/keys/values, ordered parents, canonical safe names and paths,
no duplicates, unsupported substitutions, owner-marker collision or foreign
preexisting keys. `ForceRemove` never overwrites an existing key and never
calls `RegDeleteTree`. Only the literal value-free
`NoRemove CLSID` may be shared; the shared CLSID container is not owned
and is never removed, even if created by this registration.

Every newly owned key receives `OpenATL.COMOwner` as its first REG_SZ value
(`RGS1:<ownerTag>:<canonical module path>`); all original default/named
REG_SZ values are then written unmodified apart from bounded
`%MODULE%` expansion. A write failure triggers an owner/content-verified
reverse cleanup of newly created keys. Cleanup errors override the initial
write failure; external changes are **never** silently deleted.

`UnregisterRgs` checks every expected owned key's marker, REG_SZ
type/byte count/content and complete set of direct child names **before
deleting a single key**. Foreign values/children, modified owners or
unexpected data return access denied and preserve earlier owned keys;
only verified owned keys are deleted nonrecursively in reverse order.
A missing previously owned key is accepted for recovery from interrupted
installation; duplicate unregistration is idempotent.

Native Windows tests run all four original, byte-pinned Microsoft
`.rgs` files under freshly GUID-named private
`HKCU\\Software\\OpenATL.RGSTest.*` roots. They compare the entire
resolved path/value projection, verify shared `NoRemove CLSID`, block
foreign/modified keys, and inject a deterministic write failure at every
owned node to prove reverse rollback. The test's final cleanup may use
`RegDeleteTree` **only on its uniquely owned private test root**; this is
not used in production registration.

This is **not an atomic Windows registry transaction**, and callers must
exclude concurrent installer writers. No COM initialization or registry
operation occurs during RGS parsing, `DllMain`, or object-map discovery.
The bounded executor can now be invoked from `DECLARE_REGISTRY_RESOURCEID`
using the separate mandatory `ScopedRgsRegistrationContext` installed
inside a synchronous DLL registration export. This does not register a
TypeLib object or implement the complete Microsoft ATL registrar grammar.

## Genuine `REGISTRY` resource-ID installation

`openatl/rgs_resource.h` adds `ReadRgsResource(HMODULE image, unsigned id,
RGS::Document&)` and `UpdateRegistryResource(id, BOOL)`. The first is a
**read-only** loader using exactly `FindResourceW` with resource type
`REGISTRY`, `SizeofResource`, `LoadResource`, and `LockResource`.
It handles strict UTF-8 (including BOM) and BOM-declared UTF-16 LE/BE,
rejecting malformed encoding, embedded NULs and over-budget resources before
parsing. Failed loads leave an existing output document unchanged. There
is **no** fallback to another resource type, file path or RGS engine.

`DECLARE_REGISTRY_RESOURCEID(IDR_X)` in `atlcom.h` provides the ATL
class's `static UpdateRegistry(BOOL)` hook, reusing the same immutable
`OBJECT_ENTRY_AUTO` class list and `CAtlDllModuleT` registration ordering
from #29. It requires an active `ScopedRgsRegistrationContext` created by
the **application's own registration export** with an opened HKEY,
the actual HMODULE containing the `REGISTRY` resource and an explicit
owner tag. The scope retrieves and validates the module's absolute DLL path;
its borrowed HKEY/HMODULE and owned strings exist only for that synchronous
invocation on the same module and thread. Missing/invalid/nested scopes,
wrong resource type, unknown ID and invalid resource contents fail with an
HRESULT. Class-factory lookup and UI windows need no such scope or COM init.

The native `openatl_com_server_fixture.dll` now embeds two actual
`REGISTRY` resources with class-matching CLSIDs, plus two byte-identical
Microsoft `Chain.rgs`/`Outer.rgs` resources in a Windows .rc file. Its
two ATL classes use `DECLARE_REGISTRY_RESOURCEID`, not custom
`UpdateRegistry` code or synthetic strings. The DLL exports explicitly
open `HKCU\\Software\\Classes`, construct the scoped context inside the
same DLL, and call the existing ATL module registrar. The STA/MTA client
tests verify both registered classes, ProgID lookup, a deliberately foreign
auxiliary ProgID conflict and cleanup; separate tests load the original
Microsoft REGISTRY resources from this real DLL and exercise a GUID-isolated
HKCU root.

This is a restricted source-compatible resource-ID API, **not** a general
RGS interpreter or automatic Registry/TypeLib installer. It preserves
the existing owner markers and nonrecursive deletion semantics and does not
authorize blind `ForceRemove` replacement. The caller must serialize
installer writes; no actions run under DllMain. Original Microsoft ATL
sample builds remain to be qualified in #22.

## First native IDispatch / type-info milestone

`atlcom.h` now exports a first `ATL::IDispatchImpl<T, &IID, &LIBID,
major, minor, holder>` API, backed by one `ATL::CComTypeInfoHolder`
which uses Windows' actual `LoadRegTypeLib` and
`ITypeLib::GetTypeInfoOfGuid`. `GetTypeInfoCount`,
`GetTypeInfo`, `GetIDsOfNames` and `Invoke` use real
`ITypeInfo` and enforce non-null outputs, `IID_NULL`, bounded names,
argument consistency and authentic OLE HRESULTs.

The native COM Server core test uses Windows OLEAUT32
`CreateDispTypeInfo` to construct an actual `ITypeInfo`, injected via
the optional ATL type-info-holder parameter. It verifies late-bound
case-insensitive member name resolution, `ITypeInfo::Invoke`,
return-VARIANT ownership, correct COM `IUnknown` identity,
missing LIBID/IID failures, and image-TLB mode fail-closed behavior.
It intentionally creates a true OS Automation type-info object, not a
fake IDispatch function returning success.

**This is partial ATL Automation compatibility.** There is no successful
registered/installed TypeLib native acceptance yet; the standard holder
does not cache type-info across apartment/module lifetime boundaries.
The ATL `0xffff/0xffff` image-based TypeLib mode returns
`E_NOTIMPL` instead of falling back to the registry or claiming success.
The original Microsoft COMMap/AtlSafeArray project compile/link
admission, real TypeLib deployment, BYREF/EXCEPINFO edge cases and
`DECLARE_LIBID` integration remain separately tracked under #22/#37.

## Actual file-backed TypeLib loading and module LIBID

The `CComTypeInfoHolder::GetTIFromFile(absolutePath, IID, LIBID, major,
minor, ITypeInfo**)` extension loads a real binary Windows TypeLib using
`LoadTypeLibEx(..., REGKIND_NONE)` (never `REGKIND_DEFAULT` or
`REGKIND_REGISTER`). It validates the typelib's GUID and exact version
using `ITypeLib::GetLibAttr`, then resolves the requested IID with
`GetTypeInfoOfGuid`. Missing files, mismatched LIBID/version/IID,
incomplete/relative paths and null output pointers fail without exposing a
partial interface. Every result uses ordinary COM ownership and the helper
does not cache COM pointers across apartment or module retirement.

The native test creates an **actual binary .tlb** in a GUID-owned temporary
path with Windows' `CreateTypeLib2`, adds an Automation method using
`ICreateTypeInfo`, calls `SaveAllChanges`, then exercises the **on-disk**
loaded `ITypeInfo::GetIDsOfNames`, version/LIBID/IID rejection, and proves
`LoadRegTypeLib` still fails because no typelib was installed globally.
The temporary file is removed via its owning fixture.

`DECLARE_LIBID(LIBID_...)` now provides the original ATL-derived module
`InitLibId()`, which `CAtlModuleT<T>` invokes during module construction
without any COM initialization. Its source-compatible static
`CAtlModule::m_libid` is per-image module metadata, not an authorization
for registry writes. A native module test asserts the exact LIBID and
`GetLibID` output/null-pointer contracts.

**Scope:** on-disk `REGKIND_NONE` load is independently useful and *not*
the same as image-embedded resource TLB resolution. Automatic
`0xffff/0xffff` loading from an image, genuine installed typelib lookup,
MIDL codegen, full ATL `RegisterTypeLib`/`UnRegisterTypeLib` ownership,
and original Microsoft COMMap native project builds are still missing.
No static bypass or handwritten ITypeInfo provider is introduced.

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
