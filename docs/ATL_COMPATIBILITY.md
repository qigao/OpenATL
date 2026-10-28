# ATL source compatibility

OpenATL implements a documented subset of Microsoft's ATL source APIs using
repository-owned code. Compatibility is scoped to the documented source APIs;
Microsoft ATL implementation-class binary layout is not part of this contract.
Consumers link only `OpenATL::ATL`; it supplies both ATL and adapted WTL headers.

| Header/API | Supported contract |
| --- | --- |
| `atlbase.h`, `atlcomcli.h` | Existing Win32 modules, threading policies, COM smart pointers, BSTR/VARIANT helpers |
| `atlwin.h`, `atlstr.h`, utility headers | Existing documented Windows/WTL subset; Unicode-only, C++17 |
| `atlcom.h`: `CComObjectRootBase`, `CComObjectRootEx`, `CComObjectRoot` | Nonaggregated reference counting, thread-model critical section, construction/release hooks |
| `CComObject<T>` | Concrete IUnknown, raw creation with initial reference count **zero**, typed QI, last-release destruction |
| `CComCoClass<T, &CLSID>`, `CComCreator<Object>` | Local nonaggregated creation, CLSID accessor, aggregation rejection |
| `BEGIN_COM_MAP` / `END_COM_MAP` | First simple entry supplies canonical IUnknown; typed pointer adjustments |
| `COM_INTERFACE_ENTRY`, `COM_INTERFACE_ENTRY_IID` | Interface type or explicit IID mapping |
| `COM_INTERFACE_ENTRY2`, `COM_INTERFACE_ENTRY2_IID` | Select the inheritance branch for an ambiguous base interface |
| `DECLARE_NOT_AGGREGATABLE` | Select the nonaggregated creator |
| WTL `CAppModule::Init` | `Init(hInstance)` and ordinary `Init(nullptr, hInstance)`; nonempty object maps/type-library IDs fail explicitly |

Reference-count and ownership semantics follow the corresponding ATL APIs.
`CComObject<T>::CreateInstance(&raw)` returns zero references: immediately
`AddRef`, or obtain an owned interface through `QueryInterface`, then balance
every successful acquisition. `CComCoClass::CreateInstance` returns an owned
interface with one reference. `GetUnknown()` is borrowed; QI returns ownership.
Unsupported IIDs clear output and return `E_NOINTERFACE`; null outputs return
`E_POINTER`. A temporary reference protects QI/Release during `FinalConstruct`.
HRESULT failure or exceptions destroy partial objects and call `FinalRelease`;
a throwing constructor does not call it. `CAtlException` preserves a failure
HRESULT, allocation failure maps to `E_OUTOFMEMORY`, other exceptions to
`E_UNEXPECTED`. Destructors and `FinalRelease` must not throw.

Thread models have their usual separation: `CComSingleThreadModel` requires
external serialization; `CComMultiThreadModel` synchronizes the reference
count and provides explicit `Lock`/`Unlock`. This does not marshal interfaces
or synchronize application fields automatically.

There is no hidden global DLL module. Local ATL-style objects are independent
of `OpenATL::COM::Module`. For a DLL class table, use the explicit module-aware
creator overload so each object retains a module reference until after its
user-defined destructor:

```cpp
const OpenATL::COM::ClassEntry classes[] = {
    {CLSID_MyObject, &ATL::CComObject<MyObject>::CreateInstance}
};
OpenATL::COM::Module module(classes, 1);
```

An object class uses the usual root + interfaces + COM-map pattern; it cannot
be `final` because `CComObject<T>` derives from it. The class may also inherit
`CComCoClass<T, &CLSID_MyObject>`. DLL exports and registration use the existing
[explicit server API](COM_SERVER.md). Direct local creation does not acquire a
DLL module reference; a DLL must use the module-aware creator or independently
retain its module across locally created objects and external callers.

The supported subset does not include `CComModule`, `CAtlDllModuleT`, automatic
object maps, aggregation, tear-offs, dispatch/type-library implementation,
connection points, RGS or ActiveX. Unsupported names remain undefined rather
than reporting fake success. COM map chaining and aggregate entries are not
implemented. No claim of arbitrary ATL application compatibility is made.

Existing native COM core tests cover zero-ref creation, typed/IID queries,
multiple inheritance, canonical identity, construction failure and cleanup,
threaded references, single-thread policy, and explicit module pin retirement.
The full pinned WTL SDI sample builds and runs without source patches, including
its existing `<atlcom.h>` include and `Init(nullptr, hInstance)` call.

Public behavior references:

- [CComObject](https://learn.microsoft.com/en-us/cpp/atl/reference/ccomobject-class)
- [CComObjectRootEx](https://learn.microsoft.com/en-us/cpp/atl/reference/ccomobjectrootex-class)
- [COM map entries](https://learn.microsoft.com/en-us/cpp/atl/reference/com-interface-entry-macros)
