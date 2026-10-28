#include <atlcom.h>
#include <openatl/com_registration.h>
#include "interfaces.h"

namespace {
using namespace OpenATL::COM;

HMODULE image = nullptr;
HRESULT registerClass(bool installing, REFCLSID clsid, LPCWSTR progId) noexcept {
    wchar_t path[32768]{};
    const DWORD length = ::GetModuleFileNameW(image, path, 32768);
    if (!length) return HRESULT_FROM_WIN32(::GetLastError());
    if (length >= 32768) return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    ATL::CRegKey root;
    const LSTATUS status = ::RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Classes",
                                          0, KEY_READ | KEY_WRITE, &root.m_hKey);
    if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);
    const ClassRegistration entry{clsid, path, progId,
        L"OpenATL native COM class", ThreadingModel::Both};
    return installing ? RegisterClass(root.m_hKey, entry)
                      : UnregisterClass(root.m_hKey, entry);
}

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
    static HRESULT WINAPI UpdateRegistry(BOOL install) noexcept {
        return registerClass(install != FALSE, CLSID_OpenATLValue, OpenATLTestProgId);
    }
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
    static HRESULT WINAPI UpdateRegistry(BOOL install) noexcept {
        return registerClass(install != FALSE, CLSID_OpenATLAuxValue, OpenATLAuxProgId);
    }
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
