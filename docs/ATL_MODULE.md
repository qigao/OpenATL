# ATL application modules — current implementation and server roadmap

`CAtlModule` / `CAtlModuleT<T>` own module locks and termination callbacks;
`DECLARE_LIBID(LIBID_...)` supplies the original ATL `InitLibId()`
hook. The CRTP module constructor initializes the static per-image
`CAtlModule::m_libid` metadata; this neither initializes COM nor writes
type-library registration. The native source test validates GUID identity.

`CAtlExeModuleT<T>` additionally owns a COM initialization on its construction
thread and optional explicit class-factory registrations.
`CAtlDllModuleT<T>` now supports the real DLL class-factory/unload exports
through the same `OpenATL::COM::Module` engine. Standard static
`OBJECT_ENTRY_AUTO` class discovery is implemented; automatic registry
registration from class-defined `UpdateRegistry(BOOL)` callbacks is now
supported. Bounded resource-driven RGS requires an explicit registration
context; full RGS and expanded ATL maps remain compatibility work. Legacy
`CComModule` supports bounded object maps, class-factory registration cookies,
object-main hooks and same-thread retirement; its full API remains incomplete.

One application module may be alive per binary image. Construction publishes
the borrowed `_pAtlModule` pointer before application workers start. Module
destruction and explicit `Term()` require external quiescence and zero locks.
`Lock`/`Unlock` may have many concurrent callers; configuration, registration,
message-loop execution and destruction belong to the constructing thread.
`CComObject` retains the active application module until after its user-defined
destructor, independently of an explicitly supplied `OpenATL::COM::Module` pin.
Module construction/destruction cannot race with new object construction.

Termination callback records are owned; callback code and argument referents
must outlive invocation. They run once in reverse registration
order outside the callback-list mutex. Registration accepts at most 1024
callbacks, fails on capacity/allocation errors, and rejects additions after
termination begins. Callbacks must not throw or acquire new module locks.
`Term` is idempotent. Counter misuse and destruction with outstanding locks
are lifetime violations and terminate instead of authorizing unsafe retirement.

EXE module construction calls the CRTP `InitializeCom` hook; its successful
`S_OK` or `S_FALSE` acquisition is balanced by `UninitializeCom` on the same
thread. Failed initialization is exposed by `GetInitHRESULT` and prevents
startup/registration; it must not uninitialize somebody else's apartment.
Default initialization is MTA, or STA with `_ATL_APARTMENT_THREADED` /
`_ATL_SINGLE_THREADED`. Declaring this module opts into COM initialization;
including headers and ordinary WTL `CAppModule` construction do not.

`SetClassObjects` is an OpenATL extension accepting CLSID/IUnknown descriptors.
It copies metadata, queries/retains IClassFactory references, and admits at
most 1024 entries. Input descriptors are borrowed only during that call.
Replacing a table is exclusive and prohibited while OS registration cookies
remain active. `RegisterClassObjects` uses real `CoRegisterClassObject` calls;
partial failure revokes successful registrations, retaining failed-revocation
cookies for retry. `RevokeClassObjects` releases cookies; clearing the table
releases retained factories. Empty registration/revocation returns `S_FALSE`.
Factory owners remain responsible for their own objects/server-lock lifetime.

`Run` dispatches PreMessageLoop / RunMessageLoop / PostMessageLoop, with owned
registrations revoked even on callback failure. The default pre-hook registers
explicit classes and the post-hook revokes them; no classes means `S_FALSE`
and no loop. UI applications override the hooks for their own lifecycle.
The post-hook runs when the pre-hook returns `S_OK`, including after a loop
exception. A failed pre-hook must unwind its application-owned side effects;
the module still revokes its own OS class registrations on every Run exit.
The default loop uses GetMessage/TranslateMessage/DispatchMessage and reports
GetMessage errors. Registration commands are limited to 32767 UTF-16 code
units including the terminator, then tokenized as Windows arguments;
default persistent server/AppID/type-library registration is unimplemented
and returns `E_NOTIMPL`. RGS, automatic object maps, idle-shutdown monitoring,
service modules and complete ATL EXE-server behavior are required compatibility
candidates, not permanent exclusions. No fake success is permitted.

## `CAtlDllModuleT<T>` — implemented explicit-class-table milestone

`ATL::CAtlDllModuleT<T>` is now available in `atlbase.h`. It inherits the
existing `CAtlModuleT<T>` lock/termination protocol and exposes the standard
ATL DLL-module entry points: `DllMain(DWORD, LPVOID)`,
`DllGetClassObject(REFCLSID, REFIID, LPVOID*)`,
`GetClassObject(...)`, `DllCanUnloadNow()`,
`DllRegisterServer(BOOL = TRUE)` and
`DllUnregisterServer(BOOL = TRUE)`. The application continues to export
its own unmangled `DllGetClassObject`, `DllCanUnloadNow`,
`DllRegisterServer` and `DllUnregisterServer` functions, delegating to
the module. This does not auto-export symbols.

**Current class map bridge:** call
`module.BindServer(OpenATL::COM::Module&)` with a valid, externally-owned,
immutable explicit COM class table. Binding succeeds exactly once (repeat
binding to the same module is idempotent); a second different table is
rejected without rebinding. The `OpenATL::COM::Module` and its class entries
must outlive all factories, objects, server locks and the bound DLL module.
An absent class table returns `CLASS_E_CLASSNOTAVAILABLE` and clears output;
invalid module descriptors cannot be bound.

`DllCanUnloadNow` returns `S_FALSE` when either the ATL module lock count
or the bound server's factory/object/server references are nonzero. Native
`DllGetClassObject` delegates through the derived `T::GetClassObject`
hook (as in ATL) and pins the ATL module during the synchronous lookup.
The default hook uses the explicit server table; a returned real factory
independently retains its server module.
This is a cooperative unload contract: it cannot make arbitrary
`FreeLibrary` safe during an active callback. The application must
quiesce every caller before unloading.

`DllMain` performs no COM activation, registry writes, or class registration
under the OS loader lock. Registration forwards through
`T::RegisterServer(BOOL, const CLSID*)` and
`T::UnregisterServer(BOOL, const CLSID*)`. A derived module can override
these methods; the inherited default now uses only the class-defined
`UpdateRegistry(BOOL)` methods attached to `OBJECT_ENTRY_AUTO` entries.
A missing class callback is **`E_NOTIMPL`**, not fake success.
`DECLARE_NO_REGISTRY()` deliberately opts a class out and explicitly
returns `S_OK` without installing keys. TypeLib registration uses the image-local Windows OLEAUT32 implementation.
For TRUE, the module preloads and validates the actual binary TypeLib before
any class callback, installs classes, then registers the library. Native
TypeLib registration failure reverses the successfully installed classes;
a class rollback failure takes priority over the original HRESULT. FALSE
continues to select class-only work. Unregistration preloads the TypeLib,
removes classes, then unregisters the exact LIBID/version/LCID/SYSKIND.
The Windows registry APIs are not an atomic transaction: general partial
TypeLib-write recovery and transactional unregistration are not claimed.

The global `AtlLoadTypeLib`, `AtlRegisterTypeLib` and `AtlUnRegisterTypeLib`
helpers support optional numeric \N resource indexes and counted owned outputs.
`AtlSetPerUserRegistration(true)` selects RegisterTypeLibForUser and
UnRegisterTypeLibForUser; the default selects their system-registration
counterparts. Set the image-local mode before registration and serialize
configuration/registration. Each operation snapshots its mode before class
callbacks and uses that same snapshot for RGS and TypeLib work. This does
not redirect arbitrary Win32 calls or another DLL. Native qualification
uses per-user registration and real DACL denial, without elevation or HKLM
writes. Machine-wide permission behavior remains separately unqualified.

Class-directed `DECLARE_REGISTRY_RESOURCEID` and `DECLARE_REGISTRY_RESOURCE`
load actual numeric/named `REGISTRY` resources from the module's own image.
`CAtlDllModuleT::RegisterServer` and
`UnregisterServer` supply a lazy synchronous RGS context: per-user mode opens
the current user's `Software\Classes`; default mode opens the current
token's HKCR merged Classes view with `RegOpenUserClassesRoot`. Root-access
errors propagate as HRESULTs. Custom and `DECLARE_NO_REGISTRY` callbacks do
not open this root. Once needed, the root and ownership context remain alive
through all class callbacks, TypeLib work and class rollback.

An explicit `ScopedRgsRegistrationContext` still takes priority, for example
when an installer needs an isolated root or custom owner tag. Automatic
contexts use `OpenATL.Module` plus the canonical DLL path as their ownership
identity and retain the existing parser and owner-verified executor. Direct
class callbacks use the active ATL module's static registrar; without an ATL
module they require an explicit context. Nested explicit contexts remain rejected. Installer writes
must be serialized; no work runs under `DllMain`.

`CAtlModule::UpdateRegistryFromResourceS(UINT/LPCTSTR, BOOL,
_ATL_REGMAP_ENTRY* = nullptr)` and the default `UpdateRegistryFromResource`
alias execute that same static registrar, including when called directly
outside `RegisterServer`. Existing explicit/operation contexts take priority;
otherwise the helper supplies an own-image context with the current per-user
setting. Resource names are bounded to 128 UTF-16 units and numeric IDs to
1..65535. Decoding, replacement validation and parsing finish before a default
registry root is acquired.

The replacement map is copied into owned strings before execution. Supply
at most 64 entries followed by `{nullptr, nullptr}`; the entire sentinel table
must remain readable for the call. Names are case-insensitive ASCII identifiers
of at most 64 units; data is limited to 8192 units and the complete map to
65536 units. Replacements occur only within key/name/REG_SZ data tokens.
Quotes and braces in data cannot introduce script instructions. Unknown or
duplicate names, reserved `MODULE`/`MODULE_RAW` overrides, recursive `%`
data, CR/LF and invalid expanded names fail before writes. Pointer-map strings
are NUL-terminated; counted parser-map data additionally rejects embedded NUL.
Unregistration must supply the same replacement values for ownership checks.

OpenWTL always uses its built-in static registrar. Dynamic
`UpdateRegistryFromResourceD`, `IRegistrar`/`CRegObject`,
`AddCommonRGSReplacements`, EXE-specific quoted `%MODULE%` and `%MODULE_RAW%`
semantics remain unimplemented; these APIs are not redirected to another
registrar implementation.

Public signature references: [CAtlModule registry helpers](https://learn.microsoft.com/en-us/cpp/atl/reference/catlmodule-class),
[registry macros](https://learn.microsoft.com/en-us/cpp/atl/reference/registry-macros),
and [replaceable parameters](https://learn.microsoft.com/en-us/cpp/atl/using-replaceable-parameters-the-registrar-s-preprocessor).

The existing `tests/com_server/fixture.cpp` now routes its actual COM DLL
exports through `CAtlDllModuleT`, without manually opening a root or supplying
an RGS context, and preserves real HKCU class registration,
STA/MTA activation, object identity, server locks and unload behavior. The
`atl_module.cpp` native test exercises failed/unbound/duplicate binding,
IID rejection, factory and object lifetimes, registration forwarding and
ordinary no-COM initialization.

**Implemented next step:** `OBJECT_ENTRY_AUTO(clsid, type)` registers
an immutable per-binary COM class descriptor using static storage. The
registrar only performs atomic pointer linking, with no COM call or heap
allocation during DLL initialization. Without an explicit `BindServer`,
the DLL module composes these entries **once on the first class-object
request** into the existing `OpenATL::COM::Module`. A duplicate CLSID,
invalid entry or oversized map fails closed; no entry returns
`CLASS_E_CLASSNOTAVAILABLE`. Factory, object and server-lock ownership
continues to use the **same** module engine, including `DllCanUnloadNow`.

The native COM DLL fixture uses standard `OBJECT_ENTRY_AUTO` registration
and is activated through genuine Windows STA and MTA clients; the module
test covers lookup, failure paths, duplicated CLSIDs and live
`LockServer` retirement. The old explicit `BindServer` class-table API
remains supported for existing applications; it does not create a
second COM backend.

**Class-directed registration milestone:** `OBJECT_ENTRY_AUTO` records an
optional static class `UpdateRegistry(BOOL)` callback. The DLL module takes
the same one-time class-map snapshot used for factories, sorts its classes
by CLSID for deterministic installation, and invokes real callbacks in that
order. Missing hooks fail preflight before *any* class is installed. Later
registration failure invokes inverse callbacks for earlier successes in
reverse order, returning the first rollback error in preference to the
original failure. Unregistration also proceeds in reverse order. A
`RegisterServer(FALSE, &CLSID)` call restricts the operation to one class;
unknown CLSIDs return `CLASS_E_CLASSNOTAVAILABLE`.

The real two-class COM fixture uses per-class `UpdateRegistry(BOOL)` methods
calling the existing owner-verified HKCU registrar, with STA/MTA activation
and a foreign second-class ProgID to force rollback of the first. This is
not automatic inference of ProgIDs/paths, and **not** an atomic registry
transaction: callbacks must clean up their own partial failure and callers
must serialize installer writes. The existing explicit `BindServer` table
remains usable, but its default class registration is `E_NOTIMPL`; the
owning application can provide a derived registrar.

**Not yet complete:** `CAtlComModule`, extended object-map variants,
full arbitrary RGS registration grammar, complete legacy TypeLib APIs,
aggregated classes and complete EXE/service COM Server behavior remain
separate compatibility milestones. Bounded real `REGISTRY` resource-ID
handling is now available only under an explicit scoped registration policy. Complete Microsoft ATL source compatibility is not yet claimed.
