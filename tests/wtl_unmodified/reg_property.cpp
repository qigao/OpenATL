// Real native Windows registry conformance for the original, unmodified
// WTL10 CRegProperty public API, using only source-owned OpenATL CRegKey
// and an explicitly owned ephemeral HKCU subkey. No global COM initialization
// and no HKCR/HKLM writes. Do not replace the original vcpkg WTL header.
#include <atlbase.h>
#include <atlwin.h>
#include <atlapp.h>
#include <atlmisc.h>
#include <atlreg.h>
#include <windows.h>
#include <objbase.h>
#include <string>
#include <cstdio>

#define CHECK_PROPERTY(x) do { if (!(x)) { \
    std::fprintf(stderr, "WTL_REGPROPERTY_FAIL line=%d condition=%s\n", __LINE__, #x); \
    return false; } } while (0)

namespace {
struct ScopedOwnedKey {
    std::wstring path;
    bool owned = false;
    ~ScopedOwnedKey() { (void)Close(); }
    bool Open() {
        GUID uuid{};
        if (FAILED(::CoCreateGuid(&uuid))) return false;
        wchar_t name[40]{};
        if (!::StringFromGUID2(uuid, name, 40)) return false;
        path = std::wstring(L"Software\\OpenATL.WTL.RegProperty.") + name;
        HKEY key = nullptr;
        DWORD disposition = 0;
        const LSTATUS status = ::RegCreateKeyExW(
            HKEY_CURRENT_USER, path.c_str(), 0, nullptr,
            REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE, nullptr,
            &key, &disposition);
        if (status != ERROR_SUCCESS) return false;
        const bool closed = ::RegCloseKey(key) == ERROR_SUCCESS;
        owned = closed && disposition == REG_CREATED_NEW_KEY;
        return owned;
    }
    bool Close() {
        if (!owned) return true;
        const LSTATUS status = ::RegDeleteTreeW(HKEY_CURRENT_USER, path.c_str());
        if (status == ERROR_SUCCESS) owned = false;
        return status == ERROR_SUCCESS;
    }
};
bool NoComApartment() {
    ULONG_PTR context = 0;
    return ::CoGetContextToken(&context) == CO_E_NOTINITIALIZED;
}
bool RunPropertyRoundtrip() {
    CHECK_PROPERTY(NoComApartment());
    ScopedOwnedKey root;
    CHECK_PROPERTY(root.Open());
    CHECK_PROPERTY(!root.path.empty());

    DWORD original = 0x17a2b3c4u;
    CHECK_PROPERTY(WTL::CRegProperty::WriteOne(
        root.path.c_str(), L"Counter", original));
    DWORD restored = 0;
    CHECK_PROPERTY(WTL::CRegProperty::ReadOne(
        root.path.c_str(), L"Counter", restored));
    CHECK_PROPERTY(restored == original);

    bool enabled = true;
    CHECK_PROPERTY(WTL::CRegProperty::WriteOne(
        root.path.c_str(), L"Enabled", enabled));
    bool restoredBool = false;
    CHECK_PROPERTY(WTL::CRegProperty::ReadOne(
        root.path.c_str(), L"Enabled", restoredBool));
    CHECK_PROPERTY(restoredBool == enabled);

    ATL::CString originalString(L"WTL10/OpenATL/\u03bb\u6d4b\u8bd5");
    CHECK_PROPERTY(WTL::CRegProperty::WriteOne(
        root.path.c_str(), L"Caption", originalString));
    ATL::CString restoredString;
    CHECK_PROPERTY(WTL::CRegProperty::ReadOne(
        root.path.c_str(), L"Caption", restoredString));
    CHECK_PROPERTY(restoredString == originalString.GetString());

    // Missing and malformed entries must not be accepted as valid
    // DWORD/bool properties, even if a different registry type exists.
    DWORD missing = 0;
    CHECK_PROPERTY(!WTL::CRegProperty::ReadOne(
        root.path.c_str(), L"NeverWritten", missing));
    ATL::CRegKey key;
    CHECK_PROPERTY(key.Open(HKEY_CURRENT_USER, root.path.c_str(),
                            KEY_READ | KEY_WRITE) == ERROR_SUCCESS);
    CHECK_PROPERTY(key.SetStringValue(L"WrongType", L"not-a-DWORD") == ERROR_SUCCESS);
    DWORD wrongType = 0;
    CHECK_PROPERTY(!WTL::CRegProperty::ReadOne(
        root.path.c_str(), L"WrongType", wrongType));
    DWORD type = 0;
    DWORD bytes = sizeof(DWORD);
    DWORD raw = 0;
    CHECK_PROPERTY(::RegQueryValueExW(key.m_hKey, L"Counter", nullptr,
                   &type, reinterpret_cast<LPBYTE>(&raw), &bytes) == ERROR_SUCCESS);
    CHECK_PROPERTY(type == REG_DWORD && bytes == sizeof(DWORD) && raw == original);
    CHECK_PROPERTY(key.Close() == ERROR_SUCCESS);
    CHECK_PROPERTY(root.Close());
    CHECK_PROPERTY(NoComApartment());
    return true;
}
} // namespace

bool RunRegPropertyTests() {
    for (int i = 0; i < 4; ++i) {
        if (!RunPropertyRoundtrip()) return false;
    }
    std::puts("OPENATL_WTL_REGPROPERTY_OK rounds=4 dword=1 bool=1 unicode=1 type_error=1 cleanup=1 no_com=1");
    return true;
}
