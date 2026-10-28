#include <atlbase.h>
#include <openatl/com_server.h>
#include <openatl/com_registration.h>
#include "interfaces.h"

namespace {
using namespace OpenATL::COM;
class Value final : public Object<Value, Interface<IOpenATLDerived, &IID_OpenATLDerived>,
                                       InterfaceAlias<IOpenATLValue, &IID_OpenATLValue, IOpenATLDerived>,
                                       Interface<IOpenATLSecond, &IID_OpenATLSecond>> {
public:
    explicit Value(Module& module) noexcept : Object(module) {}
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
const ClassEntry classes[] = {{CLSID_OpenATLValue, &Value::CreateInstance}};
Module module(classes, 1);
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
// Standard ATL DLL module entry points over the existing single real
// OpenATL::COM factory and registry engine. This fixture has no type library;
// its derived registration methods install/revoke its one concrete class.
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

HRESULT bindServer() noexcept { return dllModule.BindServer(module); }
} // namespace

extern "C" BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void* reserved) {
    if (reason == DLL_PROCESS_ATTACH) image = instance;
    return dllModule.DllMain(reason, reserved);
}
extern "C" HRESULT WINAPI DllGetClassObject(
    REFCLSID clsid, REFIID iid, void** output) {
    const HRESULT hr = bindServer();
    return FAILED(hr) ? hr : dllModule.DllGetClassObject(clsid, iid, output);
}
extern "C" HRESULT WINAPI DllCanUnloadNow() {
    return SUCCEEDED(bindServer()) ? dllModule.DllCanUnloadNow() : S_FALSE;
}
extern "C" HRESULT WINAPI DllRegisterServer() {
    const HRESULT hr = bindServer();
    return FAILED(hr) ? hr : dllModule.DllRegisterServer();
}
extern "C" HRESULT WINAPI DllUnregisterServer() {
    const HRESULT hr = bindServer();
    return FAILED(hr) ? hr : dllModule.DllUnregisterServer();
}
