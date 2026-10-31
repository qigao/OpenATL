#include <atlcom.h>
#include <openatl/com_registration.h>
#include "interfaces.h"
#include "fixture_registry_ids.h"

namespace {
using namespace OpenATL::COM;

HMODULE image = nullptr;
// Explicit application installer policy for this conformance DLL. The
// context is scoped to this DLL's synchronous export and is not created by
// COM activation, static registration, or DllMain.
HRESULT withRegistryContext(BOOL installing) noexcept;

// The DLL contains two classes declared with genuine ATL OBJECT_ENTRY_AUTO.
// CAtlDllModuleT invokes each class's real UpdateRegistry callback and
// continues to use the same OpenATL::COM::Module factory/owner engine.
class Value : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>,
              public ATL::CComCoClass<Value, &CLSID_OpenATLValue>,
              public IOpenATLDerived, public IOpenATLSecond {
public:
    BEGIN_COM_MAP(Value)
        COM_INTERFACE_ENTRY_IID(IID_OpenATLDerived, IOpenATLDerived)
        COM_INTERFACE_ENTRY2_IID(IID_OpenATLValue, IOpenATLValue, IOpenATLDerived)
        COM_INTERFACE_ENTRY_IID(IID_OpenATLSecond, IOpenATLSecond)
    END_COM_MAP()
    DECLARE_REGISTRY_RESOURCEID(IDR_OPENATL_VALUE)
    HRESULT STDMETHODCALLTYPE GetValue(LONG* value) noexcept override {
        if (!value) return E_POINTER;
        *value = 42;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSecond(LONG* value) noexcept override {
        if (!value) return E_POINTER;
        *value = 17;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetExtra(LONG* value) noexcept override {
        if (!value) return E_POINTER;
        *value = 99;
        return S_OK;
    }
};
OBJECT_ENTRY_AUTO(CLSID_OpenATLValue, Value)

class AuxValue : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>,
                 public ATL::CComCoClass<AuxValue, &CLSID_OpenATLAuxValue>,
                 public IOpenATLValue {
public:
    BEGIN_COM_MAP(AuxValue)
        COM_INTERFACE_ENTRY_IID(IID_OpenATLValue, IOpenATLValue)
    END_COM_MAP()
    DECLARE_REGISTRY_RESOURCEID(IDR_OPENATL_AUX)
    HRESULT STDMETHODCALLTYPE GetValue(LONG* value) noexcept override {
        if (!value) return E_POINTER;
        *value = 84;
        return S_OK;
    }
};
OBJECT_ENTRY_AUTO(CLSID_OpenATLAuxValue, AuxValue)

// No application-specific registration override or manual BindServer.
class FixtureAtlModule : public ATL::CAtlDllModuleT<FixtureAtlModule> {};
FixtureAtlModule dllModule;

HRESULT withRegistryContext(BOOL installing) noexcept {
    ATL::CRegKey root;
    const LSTATUS state = ::RegOpenKeyExW(
        HKEY_CURRENT_USER, L"Software\\Classes", 0,
        KEY_READ | KEY_WRITE, &root.m_hKey);
    if (state != ERROR_SUCCESS) return HRESULT_FROM_WIN32(state);
    // Under HKCU only, explicitly selected by this application's installer.
    // The same context is visible to the two class callbacks in this image;
    // client/test EXE TLS and other DLLs cannot accidentally inherit it.
    ScopedRgsRegistrationContext scope(root.m_hKey, image,
                                        L"OpenATLNativeFixture");
    if (FAILED(scope.Status())) return scope.Status();
#if defined(OPENATL_ORIGINAL_COMMAP_MIDL)
    bool previous = false;
    HRESULT hr = ATL::AtlGetPerUserRegistration(&previous);
    if (FAILED(hr)) return hr;
    struct RestoreMode {
        bool previous;
        ~RestoreMode() { ATL::AtlSetPerUserRegistration(previous); }
    } restore{previous};
    ATL::AtlSetPerUserRegistration(true);
    return installing ? dllModule.DllRegisterServer()
                      : dllModule.DllUnregisterServer();
#else
    // Cross-host fixtures without native MIDL still select class-only work.
    return installing ? dllModule.DllRegisterServer(FALSE)
                      : dllModule.DllUnregisterServer(FALSE);
#endif
}
} // namespace

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void* reserved) {
    if (reason == DLL_PROCESS_ATTACH) image = instance;
    return dllModule.DllMain(reason, reserved);
}
extern "C" HRESULT WINAPI DllGetClassObject(
    REFCLSID clsid, REFIID iid, void** output) {
    return dllModule.DllGetClassObject(clsid, iid, output);
}
extern "C" HRESULT WINAPI DllCanUnloadNow() {
    return dllModule.DllCanUnloadNow();
}
extern "C" HRESULT WINAPI DllRegisterServer() {
    return withRegistryContext(TRUE);
}
extern "C" HRESULT WINAPI DllUnregisterServer() {
    return withRegistryContext(FALSE);
}
