#include <atlconv.h> // Also qualify the standalone public conversion header.
#include <atlbase.h>
#include <atlstr.h>
#include <openatl/unicode.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>

using namespace OpenATL::Unicode;
#define CHECK(expression) do { if (!(expression)) { \
    std::fprintf(stderr, "OPENATL_UNICODE_FAIL line=%d expression=%s acp=%u\n", \
                 __LINE__, #expression, ::GetACP()); return false; } } while (0)

static bool conversionObjects() {
    static_assert(!std::is_copy_constructible<ATL::CA2W>::value);
    static_assert(!std::is_copy_assignable<ATL::CW2A>::value);
    // These standard declarations must compile without USES_CONVERSION.
    ATL::CW2A utf8(L"\u6771\U0001F600", CP_UTF8);
    ATL::CA2W wide(utf8, CP_UTF8);
    CHECK(std::strcmp(utf8, "\xE6\x9D\xB1\xF0\x9F\x98\x80") == 0);
    CHECK(std::wcscmp(wide, L"\u6771\U0001F600") == 0);
    CHECK(utf8.m_psz == utf8.m_szBuffer && wide.m_psz == wide.m_szBuffer);
    ATL::CA2W euro("\x80", 1252);
    ATL::CW2A ansi(euro, 1252);
    CHECK(std::wcscmp(euro, L"\u20AC") == 0 && std::strcmp(ansi, "\x80") == 0);
    ATL::CA2T text("ASCII");
    ATL::CT2A narrow(text);
    CHECK(std::strcmp(narrow, "ASCII") == 0);

    ATL::CA2WEX<4> exactWide("abc", CP_UTF8);
    ATL::CW2AEX<4> exactNarrow(L"abc", CP_UTF8);
    CHECK(exactWide.m_psz == exactWide.m_szBuffer &&
          exactNarrow.m_psz == exactNarrow.m_szBuffer);
    ATL::CA2WEX<4> grownWide("abcd", CP_UTF8);
    ATL::CW2AEX<4> grownNarrow(L"abcd", CP_UTF8);
    CHECK(grownWide.m_psz != grownWide.m_szBuffer &&
          grownNarrow.m_psz != grownNarrow.m_szBuffer);
    const std::string longBytes(4096, 'x');
    for (int i = 0; i != 50; ++i) {
        ATL::CA2WEX<8> large(longBytes.c_str(), CP_UTF8);
        ATL::CW2AEX<8> roundTrip(large, CP_UTF8);
        CHECK(large.m_psz != large.m_szBuffer &&
              roundTrip.m_psz != roundTrip.m_szBuffer);
        CHECK(std::strcmp(roundTrip, longBytes.c_str()) == 0);
    }
    CHECK(std::wcscmp(grownWide, L"abcd") == 0 &&
          std::strcmp(grownNarrow, "abcd") == 0);
    LPWSTR writableWide = grownWide;
    LPSTR writableNarrow = grownNarrow;
    writableWide[0] = L'z';
    writableNarrow[0] = 'z';
    CHECK(std::wcscmp(grownWide, L"zbcd") == 0 &&
          std::strcmp(grownNarrow, "zbcd") == 0);
    ATL::CA2W nullWide(nullptr);
    ATL::CW2A nullNarrow(nullptr);
    ATL::CA2WEX<1> emptyWide("", CP_UTF8);
    ATL::CW2AEX<1> emptyNarrow(L"", CP_UTF8);
    CHECK(!nullWide.m_psz && !nullNarrow.m_psz);
    CHECK(emptyWide.m_psz == emptyWide.m_szBuffer && !*emptyWide.m_psz);
    CHECK(emptyNarrow.m_psz == emptyNarrow.m_szBuffer && !*emptyNarrow.m_psz);
    bool invalidBytes = false, invalidWide = false, lossy = false, invalidPage = false;
    try { ATL::CA2W bad("\xC0\xAF", CP_UTF8); }
    catch (const ATL::CAtlException& e) {
        invalidBytes = e.m_hr == HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION);
    }
    const wchar_t surrogate[] = {wchar_t(0xd800), 0};
    try { ATL::CW2A bad(surrogate, CP_UTF8); }
    catch (const ATL::CAtlException& e) {
        invalidWide = e.m_hr == HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION);
    }
    try { ATL::CW2A bad(L"\u221E", 1252); }
    catch (const ATL::CAtlException& e) {
        lossy = e.m_hr == HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION);
    }
    try { ATL::CA2W bad("x", 0xffffffffu); }
    catch (const ATL::CAtlException& e) { invalidPage = FAILED(e.m_hr); }
    CHECK(invalidBytes && invalidWide && lossy && invalidPage);
    return true;
}

static bool boundaries() {
    const std::string utf8("A\0\xF0\x9F\x98\x80\xE6\x9D\xB1", 9);
    const std::wstring utf16(L"A\0\U0001F600\u6771", 5);
    std::wstring wide = L"before";
    std::string bytes = "before";
    CHECK(Utf8ToUtf16(utf8, wide) == S_OK && wide == utf16);
    CHECK(Utf16ToUtf8(wide, bytes) == S_OK && bytes == utf8);
    CHECK(Utf8ToUtf16({}, wide) == S_OK && wide.empty());
    CHECK(Utf16ToUtf8({}, bytes) == S_OK && bytes.empty());
    CHECK(Utf8ToUtf16("\xEF\xBB\xBFx", wide) == S_OK && wide == L"\uFEFFx");
    const std::string malformed[] = {"\xC0\xAF", "\xE0\x80\x80", "\xED\xA0\x80",
                                      "\xF4\x90\x80\x80", "\xE2\x82", "\x80"};
    for (const auto& input : malformed) {
        wide = L"unchanged";
        CHECK(Utf8ToUtf16(input, wide) == HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION));
        CHECK(wide == L"unchanged");
    }
    const std::wstring invalidWide[] = {std::wstring(1, wchar_t(0xd800)),
        std::wstring(1, wchar_t(0xdc00)), std::wstring({wchar_t(0xd800), L'a'})};
    for (const auto& input : invalidWide) {
        bytes = "unchanged";
        CHECK(Utf16ToUtf8(input, bytes) == HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION));
        CHECK(bytes == "unchanged");
        CHECK(FAILED(Utf16ToCodePage(1252, input, bytes)) && bytes == "unchanged");
    }
    CHECK(CodePageToUtf16(1252, std::string_view("\x80", 1), wide) == S_OK && wide == L"\u20AC");
    CHECK(Utf16ToCodePage(1252, wide, bytes) == S_OK && bytes == "\x80");
    bytes = "unchanged";
    CHECK(Utf16ToCodePage(1252, L"\u221E", bytes) == HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION));
    CHECK(bytes == "unchanged");
    wide = L"unchanged";
    CHECK(CodePageToUtf16(932, std::string_view("\x81", 1), wide) ==
           HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION) && wide == L"unchanged");

    ATL::CString string;
    CHECK(string.AssignUtf8(utf8) == S_OK && string.GetLength() == 5);
    CHECK(string.ToUtf8(bytes) == S_OK && bytes == utf8);
    CHECK(FAILED(string.AssignUtf8("\xC0\xAF")) && string.GetLength() == 5);
    CHECK(string.ToUtf8(bytes) == S_OK && bytes == utf8);
    ATL::CComBSTR bstr;
    CHECK(bstr.AssignUtf8(utf8) == S_OK && bstr.Length() == 5);
    BSTR owned = bstr.m_str;
    CHECK(FAILED(bstr.AssignUtf8("\xC0\xAF")) && bstr.m_str == owned);
    CHECK(bstr.ToUtf8(bytes) == S_OK && bytes == utf8);
    ATL::CString ascii("ASCII");
    ATL::CComBSTR asciiBstr("ASCII");
    CHECK(ascii.Compare(L"ASCII") == 0 && asciiBstr.Length() == 5);
    ATL::CString nullString(static_cast<LPCSTR>(nullptr));
    ATL::CComBSTR nullBstr(static_cast<LPCSTR>(nullptr));
    CHECK(nullString.IsEmpty() && nullBstr.m_str == nullptr);
    ATL::CString empty("");
    ATL::CComBSTR emptyBstr("");
    CHECK(empty.IsEmpty() && emptyBstr.Length() == 0);
    ATL::CString decomposed(L"e\u0301");
    ATL::CComBSTR decomposedBstr(L"e\u0301"), composedBstr(L"\u00E9");
    CHECK(decomposed.Compare(L"\u00E9") != 0 && decomposedBstr != composedBstr);

    if (::GetACP() == CP_UTF8) {
        bool stringRejected = false, bstrRejected = false;
        try { ATL::CString bad("\xC0\xAF"); }
        catch (const ATL::CAtlException& e) {
            stringRejected = e.m_hr == HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION);
        }
        try { ATL::CComBSTR bad("\xC0\xAF"); }
        catch (const ATL::CAtlException& e) {
            bstrRejected = e.m_hr == HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION);
        }
        CHECK(stringRejected && bstrRejected);
        CHECK(FAILED(string.AssignAnsi("\xC0\xAF")) && string.GetLength() == 5);
        CHECK(FAILED(bstr.AssignAnsi("\xC0\xAF")) && bstr.m_str == owned);
        ATL::CString accent("\xC3\xA9");
        ATL::CComBSTR accentBstr("\xC3\xA9");
        CHECK(accent.Compare(L"\u00E9") == 0 && accentBstr.Length() == 1 && accentBstr.m_str[0] == 0x00e9);
    }

    ATL::CConversionScope scope;
    const wchar_t* first = scope.ToWideUtf8("\xE6\x9D\xB1\xF0\x9F\x98\x80");
    const char* second = scope.ToUtf8(L"\u6771\U0001F600");
    CHECK(first && second);
    for (int i = 0; i < 100; ++i) {
        CHECK(scope.ToWide("ASCII") && scope.ToAnsi(L"ASCII"));
        CHECK(scope.ToWideUtf8("more") && scope.ToUtf8(L"more"));
    }
    CHECK(std::wcscmp(first, L"\u6771\U0001F600") == 0);
    CHECK(std::strcmp(second, "\xE6\x9D\xB1\xF0\x9F\x98\x80") == 0);
    CHECK(!scope.ToWideUtf8("\xC0\xAF") && ::GetLastError() == ERROR_NO_UNICODE_TRANSLATION);
    CHECK(!scope.ToWideUtf8(nullptr) && !scope.ToUtf8(nullptr));
    return true;
}


int main(int argc, char** argv) {
    const bool requireUtf8 = argc == 2 && std::strcmp(argv[1], "--utf8-acp") == 0;
    if (argc != 1 && !requireUtf8) return 2;
    if (requireUtf8 && ::GetACP() != CP_UTF8) return 3;
    ULONG_PTR token = 0;
    if (::CoGetContextToken(&token) != CO_E_NOTINITIALIZED ||
        !boundaries() || !conversionObjects()) return 1;
    if (::CoGetContextToken(&token) != CO_E_NOTINITIALIZED) return 1;
    std::printf("OPENATL_UNICODE_OK utf8=1 utf16=1 acp=1 ownership=1 win32=1\n");
    return 0;
}
