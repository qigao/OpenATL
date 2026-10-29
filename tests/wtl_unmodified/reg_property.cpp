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
#include <array>
#include <cstring>
#include <cwchar>
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
// Original WTL mapped-property CRTP callbacks. All accesses use the
// original pinned CRegPropertyImpl/REG_PROPERTY macros, not a second parser.
struct MapProperties : WTL::CRegPropertyImpl<MapProperties> {
    DWORD count = 0;
    bool enabled = false;
    ATL::CString caption;
    int reads = 0, writes = 0, readErrors = 0, writeErrors = 0;
    LSTATUS lastReadError = ERROR_SUCCESS;
    LSTATUS lastWriteError = ERROR_SUCCESS;
    std::wstring lastReadName;

    BEGIN_REGPROP_MAP(MapProperties)
        REG_PROPERTY(L"MapCount", count)
        REG_PROPERTY(L"MapEnabled", enabled)
        REG_PROPERTY(L"MapCaption", caption)
    END_REGPROP_MAP()

    void OnRead(LPCTSTR) { ++reads; }
    void OnWrite(LPCTSTR) { ++writes; }
    void OnReadError(LPCTSTR name, LSTATUS error) {
        ++readErrors;
        lastReadError = error;
        lastReadName = name ? name : L"";
    }
    void OnWriteError(LPCTSTR, LSTATUS error) {
        ++writeErrors;
        lastWriteError = error;
    }
};

struct ScopedFont {
    HFONT value = nullptr;
    ~ScopedFont() { if (value) ::DeleteObject(value); }
    ScopedFont(const ScopedFont&) = delete;
    ScopedFont& operator=(const ScopedFont&) = delete;
    ScopedFont() = default;
};

bool RunExtendedPropertyRoundtrip(const std::wstring& root, ATL::CRegKey& key) {
    const std::array<BYTE, 9> blob = {0x00,0x7f,0x80,0xff,0x51,0x00,0x34,0x9a,0x11};
    WTL::CRegProperty::BinaryProp output{};
    output.pBinary = const_cast<BYTE*>(blob.data());
    output.uSize = static_cast<ULONG>(blob.size());
    CHECK_PROPERTY(WTL::CRegProperty::WriteOne(root.c_str(), L"Blob", output));
    std::array<BYTE, 9> got{};
    WTL::CRegProperty::BinaryProp input{};
    input.pBinary = got.data();
    input.uSize = static_cast<ULONG>(got.size());
    CHECK_PROPERTY(WTL::CRegProperty::ReadOne(root.c_str(), L"Blob", input));
    CHECK_PROPERTY(input.uSize == blob.size() && got == blob);

    // WTL's public BinaryProp/CharArrayProp contracts reject insufficient
    // caller-owned buffers with ERROR_OUTOFMEMORY, never partial copies.
    WTL::CRegProperty typed;
    CHECK_PROPERTY(typed.OpenRegKey(root.c_str(), false) == ERROR_SUCCESS);
    std::array<BYTE, 4> smallBlob{};
    smallBlob.fill(0xa5);
    WTL::CRegProperty::BinaryProp smallBinary{};
    smallBinary.pBinary = smallBlob.data();
    smallBinary.uSize = static_cast<ULONG>(smallBlob.size());
    CHECK_PROPERTY(typed.ReadProp(L"Blob", smallBinary) == ERROR_OUTOFMEMORY);
    CHECK_PROPERTY(smallBinary.uSize == smallBlob.size());
    for (BYTE b : smallBlob) CHECK_PROPERTY(b == 0xa5);

    wchar_t originalText[] = L"WTL-CharArray-\u03bb\u6d4b\u8bd5";
    WTL::CRegProperty::CharArrayProp writeText{};
    writeText.lpstrText = originalText;
    writeText.uSize = sizeof(originalText) / sizeof(originalText[0]);
    CHECK_PROPERTY(WTL::CRegProperty::WriteOne(root.c_str(), L"CharArray", writeText));
    std::array<wchar_t, 64> charBuffer{};
    WTL::CRegProperty::CharArrayProp readText{};
    readText.lpstrText = charBuffer.data();
    readText.uSize = static_cast<ULONG>(charBuffer.size());
    CHECK_PROPERTY(WTL::CRegProperty::ReadOne(root.c_str(), L"CharArray", readText));
    CHECK_PROPERTY(std::wcscmp(charBuffer.data(), originalText) == 0);
    CHECK_PROPERTY(readText.uSize <= charBuffer.size());
    std::array<wchar_t, 3> tinyText = {L'X',L'Y',L'Z'};
    WTL::CRegProperty::CharArrayProp shortText{};
    shortText.lpstrText = tinyText.data();
    shortText.uSize = static_cast<ULONG>(tinyText.size());
    CHECK_PROPERTY(typed.ReadProp(L"CharArray", shortText) == ERROR_OUTOFMEMORY);
    CHECK_PROPERTY(shortText.uSize == tinyText.size() &&
                   tinyText[0] == L'X' && tinyText[1] == L'Y' && tinyText[2] == L'Z');
    typed.CloseRegKey();

    LOGFONTW fontDesc{};
    fontDesc.lfHeight = -17;
    fontDesc.lfWeight = FW_BOLD;
    fontDesc.lfCharSet = DEFAULT_CHARSET;
    ::lstrcpyW(fontDesc.lfFaceName, L"Arial");
    ScopedFont originalFont;
    originalFont.value = ::CreateFontIndirectW(&fontDesc);
    CHECK_PROPERTY(originalFont.value != nullptr);
    CHECK_PROPERTY(WTL::CRegProperty::WriteOne(root.c_str(), L"Font", originalFont.value));
    DWORD type = 0;
    ULONG size = 0;
    CHECK_PROPERTY(key.QueryValue(L"Font", &type, nullptr, &size) == ERROR_SUCCESS);
    CHECK_PROPERTY(type == REG_BINARY && size == sizeof(LOGFONTW));
    ScopedFont restoredFont;
    CHECK_PROPERTY(WTL::CRegProperty::ReadOne(root.c_str(), L"Font", restoredFont.value));
    CHECK_PROPERTY(restoredFont.value != nullptr);
    LOGFONTW fontResult{};
    CHECK_PROPERTY(::GetObjectW(restoredFont.value, sizeof(fontResult), &fontResult) ==
                   sizeof(fontResult));
    CHECK_PROPERTY(fontResult.lfHeight == fontDesc.lfHeight &&
                   fontResult.lfWeight == fontDesc.lfWeight &&
                   ::lstrcmpW(fontResult.lfFaceName, fontDesc.lfFaceName) == 0);

    MapProperties write{};
    write.count = 0x7f3095;
    write.enabled = true;
    write.caption = L"WTL map / \u03bb\u6d4b\u8bd5";
    write.Write(root.c_str());
    CHECK_PROPERTY(write.writes == 1 && write.writeErrors == 0 && write.reads == 0);
    MapProperties read{};
    read.Read(root.c_str());
    CHECK_PROPERTY(read.reads == 1 && read.readErrors == 0 &&
                   read.count == write.count && read.enabled == write.enabled &&
                   read.caption == write.caption.GetString());

    // A missing mapped property preserves the caller's default and causes
    // no OnReadError; a wrong registry type causes exactly one callback,
    // while valid neighboring properties still read normally.
    CHECK_PROPERTY(key.DeleteValue(L"MapEnabled") == ERROR_SUCCESS);
    MapProperties optional{};
    optional.enabled = true;
    optional.Read(root.c_str());
    CHECK_PROPERTY(optional.reads == 1 && optional.readErrors == 0 &&
                   optional.enabled && optional.count == write.count);
    CHECK_PROPERTY(key.SetStringValue(L"MapCount", L"wrong type") == ERROR_SUCCESS);
    MapProperties invalid{};
    invalid.count = 123;
    invalid.Read(root.c_str());
    CHECK_PROPERTY(invalid.reads == 1 && invalid.readErrors == 1 &&
                   invalid.lastReadError == ERROR_INVALID_DATATYPE &&
                   invalid.lastReadName == L"MapCount" &&
                   invalid.count == 123 && invalid.caption == write.caption.GetString());
    return true;
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
    // WTL10 explicitly accepts an absent optional setting while preserving
    // the caller's default. The underlying typed ReadProp retains the error.
    DWORD missing = 0x9a13b5c7u;
    CHECK_PROPERTY(WTL::CRegProperty::ReadOne(
        root.path.c_str(), L"NeverWritten", missing));
    CHECK_PROPERTY(missing == 0x9a13b5c7u);
    ATL::CRegKey key;
    CHECK_PROPERTY(key.Open(HKEY_CURRENT_USER, root.path.c_str(),
                            KEY_READ | KEY_WRITE) == ERROR_SUCCESS);
    CHECK_PROPERTY(key.SetStringValue(L"WrongType", L"not-a-DWORD") == ERROR_SUCCESS);
    DWORD wrongType = 0;
    CHECK_PROPERTY(!WTL::CRegProperty::ReadOne(
        root.path.c_str(), L"WrongType", wrongType));

    WTL::CRegProperty typed;
    CHECK_PROPERTY(typed.OpenRegKey(root.path.c_str(), false) == ERROR_SUCCESS);
    DWORD untouched = 0x6b9c01efu;
    CHECK_PROPERTY(typed.ReadProp(L"NeverWritten", untouched) == ERROR_FILE_NOT_FOUND);
    CHECK_PROPERTY(untouched == 0x6b9c01efu);
    CHECK_PROPERTY(typed.ReadProp(L"WrongType", untouched) == ERROR_INVALID_DATATYPE);
    CHECK_PROPERTY(untouched == 0x6b9c01efu);
    typed.CloseRegKey();

    CHECK_PROPERTY(RunExtendedPropertyRoundtrip(root.path, key));

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
    std::puts("OPENATL_WTL_REGPROPERTY_OK rounds=4 dword=1 bool=1 unicode=1 optional_missing=1 type_error=1 binary=1 char_array=1 font=1 map=1 callbacks=1 cleanup=1 no_com=1");
    return true;
}
