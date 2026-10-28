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
