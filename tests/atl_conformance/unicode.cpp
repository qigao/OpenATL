#include <atlbase.h>
#include <atlstr.h>
#include <openatl/unicode.h>
#include <cstdio>
#include <cstring>
#include <string>

using namespace OpenATL::Unicode;
#define CHECK(expression) do { if (!(expression)) { \
    std::fprintf(stderr, "OPENATL_UNICODE_FAIL line=%d expression=%s acp=%u\n", \
                 __LINE__, #expression, ::GetACP()); return false; } } while (0)

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

#if defined(OPENATL_USE_SALTS_UNICODE) && OPENATL_USE_SALTS_UNICODE
static bool saltsText() {
    CHECK(std::strcmp(salts_unicode_version(), "17.0.0") == 0);
    std::string output = "before";
    CHECK(NormalizeNfcUtf8("e\xCC\x81", output) == S_OK && output == "\xC3\xA9");
    CHECK(NormalizeNfcUtf8(std::string_view("e\xCC\x81\0x", 5), output) == S_OK &&
           output == std::string("\xC3\xA9\0x", 4));
    CHECK(CaseFoldUtf8("Stra\xC3\x9F" "e", output) == S_OK && output == "strasse");
    CHECK(CaseFoldUtf8("E\xCC\x81", output) == S_OK && output == "e\xCC\x81");
    CHECK(CaseFoldUtf8("\xC4\xB0\xCE\xA3\xCF\x82", output) == S_OK &&
           output == "i\xCC\x87\xCF\x83\xCF\x83");
    CHECK(TrimUtf8("\xC2\xA0" "hello\xE3\x80\x80", output) == S_OK && output == "hello");
    output = "e\xCC\x81";
    CHECK(NormalizeNfcUtf8(output, output) == S_OK && output == "\xC3\xA9");
    output = "Stra\xC3\x9F" "e";
    CHECK(CaseFoldUtf8(output, output) == S_OK && output == "strasse");
    output = "unchanged";
    CHECK(FAILED(NormalizeNfcUtf8("\xC0\xAF", output)) && output == "unchanged");
    CHECK(FAILED(CaseFoldUtf8("\xC0\xAF", output)) && output == "unchanged");
    CHECK(FAILED(TrimUtf8("ok\xC0\xAF", output)) && output == "unchanged");
    CHECK(NormalizeNfcUtf8("ab", output, 1) == HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW) &&
           output == "unchanged");
    CHECK(CaseFoldUtf8("ab", output, 1) == HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW));
    CHECK(TrimUtf8("ab", output, 1) == HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW));
    CHECK(NormalizeNfcUtf8({}, output) == S_OK && output.empty());
    CHECK(CaseFoldUtf8({}, output) == S_OK && output.empty());
    CHECK(TrimUtf8({}, output) == S_OK && output.empty());
    std::wstring wide = L"before";
    CHECK(NormalizeNfcUtf16(L"e\u0301", wide) == S_OK && wide == L"\u00E9");
    CHECK(CaseFoldUtf16(L"Stra\u00DFe", wide) == S_OK && wide == L"strasse");
    CHECK(FAILED(NormalizeNfcUtf16(std::wstring(1, wchar_t(0xd800)), wide)) && wide == L"strasse");
    return true;
}
#endif

int main(int argc, char** argv) {
    const bool requireUtf8 = argc == 2 && std::strcmp(argv[1], "--utf8-acp") == 0;
    if (argc != 1 && !requireUtf8) return 2;
    if (requireUtf8 && ::GetACP() != CP_UTF8) return 3;
    ULONG_PTR token = 0;
    if (::CoGetContextToken(&token) != CO_E_NOTINITIALIZED || !boundaries()) return 1;
    int salts = 0;
#if defined(OPENATL_USE_SALTS_UNICODE) && OPENATL_USE_SALTS_UNICODE
    if (!saltsText()) return 1;
    salts = 1;
#endif
    if (::CoGetContextToken(&token) != CO_E_NOTINITIALIZED) return 1;
    std::printf("OPENATL_UNICODE_OK utf8=1 utf16=1 acp=1 ownership=1 salts=%d\n", salts);
    return 0;
}
