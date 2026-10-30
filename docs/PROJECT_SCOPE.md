# OpenATL project charter — ATL compatibility

**Canonical scope (2026-10-29): OpenATL implements ATL-compatible public headers and semantics for both COM Client and COM Server, Win32 windowing, ATL utility classes and Windows Template Library (WTL) consumers.** Previous UI-only or COM-client-only restrictions are superseded.

## Required capabilities

- **COM Client:** COM apartment ownership, system/component activation, interface identity and smart-pointer ownership, BSTR/VARIANT/SAFEARRAY, Automation, cross-apartment integration and event sinks.
- **COM Server:** class factories and IClassFactory, nonaggregated/aggregated objects, standard COM maps and object maps, CAtlModule/CAtlDllModuleT/CAtlExeModuleT, CLSID/ProgID/AppID registration and RGS, class-object activation, DLL/EXE lifecycle, IDispatch/type libraries and connection-point server components.
- **ATL/WTL:** source-compatible public ATL utility and windowing APIs with official WTL 10 applications and real downstream consumers.

These are **project goals, not claims of present implementation**. The [compatibility matrix](ATL_COMPATIBILITY.md) records what is available, missing and qualified. Both client and server API families are first-class; code from PR #17 is a supported starting point, not a subsystem to move out.

## Current evidence and gaps

- Repository-owned ATL headers under include/ are the production implementation, not a silent Microsoft ATL fallback.
- The public source-tree target is OpenWTL::WTL, which includes the internal openatl_atl backend. That backend supports COM Client helpers and real explicit in-process COM Server factories, locks, registry registration and Windows native STA/MTA activation.
- Existing native tests exercise both sides; no-COM-init Win32/WTL UI continues to be a distinct supported case.
- Standard CAtlDllModuleT, automatic/chained COM maps, aggregation, RGS, Automation/type libraries, SAFEARRAY wrappers and portions of ActiveX/control support are **unimplemented compatibility gaps**, not prohibited capabilities.
- 11 official WTL samples have Win32 builds; 37 non-MFC Microsoft ATL projects remain a separate incomplete compatibility target.

## Acceptance

Original unmodified [ATL/WTL examples](OFFICIAL_SAMPLES.md) and downstream consumers are the conformance sources; compile/link is not a substitute for native behavior. Qualify real Windows COM activation, apartment lifetime, identity, module locks/unload and registry rollback with MSVC and llvm-mingw. Distinguish native Windows execution from Linux/macOS cross-compilation and bind all CI claims to the actual HEAD.

Source ownership, safe lifetimes, provenance, exact-head CI and truthful unsupported HRESULTs remain essential engineering criteria, not limits excluding COM Server.

Current build integration uses source CMake; an installed/exported SDK is not implemented and its desirability is an open decision in issue #4, not a permanent ban. UI code without COM activity is not required to initialize a COM apartment.

Tracking: [full ATL roadmap #20](https://github.com/qigao/OpenATL/issues/20), [COM Server #3](https://github.com/qigao/OpenATL/issues/3), [Weasel consumer #18](https://github.com/qigao/OpenATL/issues/18).
