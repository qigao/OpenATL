#include <atlcom.h>
#include <openatl/com_server.h>
#include <openatl/com_registration.h>
#include "interfaces.h"

namespace {
using namespace OpenATL::COM;
// The actual DLL exported class is declared with standard ATL source
// syntax. Its IClassFactory and module references still come exclusively
// from OpenATL::COM::Module, composed from the static object map on demand.
class Value : public ATL::CComObjectRootEx<ATL::CComMultiThreadModel>,
              public ATL::CComCoClass<Value, &CLSID_OpenATLValue>,
              public IOpenATLDerived, public IOpenATLSecond {
public:
    BEGIN_COM_MAP(Value)
        COM_INTERFACE_ENTRY_IID(IID_OpenATLDerived, IOpenATLDerived)
        COM_INTERFACE_ENTRY2_IID(IID_OpenATLValue, IOpenATLValue, IOpenATLDerived)
        COM_INTERFACE_ENTRY_IID(IID_OpenATLSecond, IOpenATLSecond)
    END_COM_MAP()
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

HMODULE image = nullptr;

HRESULT registration(bool install) noexcept {
    wchar_t path[32768]{};
    DWORD length = ::GetModuleFileNameW(image, path, 32768);
    if (!length) return HRESULT_FROM_WIN32(::GetLastError());
    if (length >= 32768) return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    ATL::CRegKey root;
    LSTATUS status = ::RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Classes", 0,
                                    KEY_READ | KEY_WRITE, &root.m_hKey);
    if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);
    ClassRegistration entry{CLSID_OpenATLValue, path, OpenATLTestProgId,
                             L"OpenATL native COM fixture", ThreadingModel::Both};
    return install ? RegisterClass(root.m_hKey, entry) : UnregisterClass(root.m_hKey, entry);
}
// Standard ATL DLL module and automatic class map share the same real
// OpenATL::COM factory/registration engine. The fixture has no type library;
// derived registration hooks install/revoke this concrete class.
class FixtureAtlModule : public ATL::CAtlDllModuleT<FixtureAtlModule> {
public:
    HRESULT RegisterServer(BOOL, const CLSID* selected = nullptr) noexcept {
        if (selected && !IsEqualCLSID(*selected, CLSID_OpenATLValue))
            return CLASS_E_CLASSNOTAVAILABLE;
        return registration(true);
    }
    HRESULT UnregisterServer(BOOL, const CLSID* selected = nullptr) noexcept {
        if (selected && !IsEqualCLSID(*selected, CLSID_OpenATLValue))
            return CLASS_E_CLASSNOTAVAILABLE;
        return registration(false);
    }
};
FixtureAtlModule dllModule;
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
    return dllModule.DllRegisterServer();
}
extern "C" HRESULT WINAPI DllUnregisterServer() {
    return dllModule.DllUnregisterServer();
}
