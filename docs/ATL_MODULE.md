# Modern application module protocol

`CAtlModule` / `CAtlModuleT<T>` own module locks and termination callbacks;
`CAtlExeModuleT<T>` additionally owns a COM initialization on its construction
thread and optional explicit class-factory registrations. The obsolete
`CComModule` and automatic object maps remain absent.

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
default persistent server/AppID/type-library registration is unsupported and
returns `E_NOTIMPL`. No RGS, automatic object maps, idle-shutdown monitor,
service module or complete ATL EXE-server framework is claimed.

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
under the OS loader lock. Registration forwards to a derived
`T::RegisterServer(BOOL, const CLSID*)` /
`T::UnregisterServer(BOOL, const CLSID*)` implementation. The inherited
default is **`E_NOTIMPL`**, not fake success, until concrete registrar and
type-library semantics are supplied by that server.

The existing `tests/com_server/fixture.cpp` now routes its actual COM DLL
exports through `CAtlDllModuleT` and preserves real HKCU class registration,
STA/MTA activation, object identity, server locks and unload behavior. The
`atl_module.cpp` native test exercises failed/unbound/duplicate binding,
IID rejection, factory and object lifetimes, registration forwarding and
ordinary no-COM initialization.

**Not yet complete:** standard ATL automatic `OBJECT_ENTRY_AUTO` maps,
`CAtlComModule`, RGS, registered type libraries, aggregated classes and
complete EXE/service COM server behavior remain separate compatibility
milestones. The explicit class-table bridge is temporary API surface for
the already-implemented factory engine, not a secondary provider or
a claim of complete Microsoft ATL compatibility.
