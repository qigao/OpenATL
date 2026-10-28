// Strict counted Windows encoding boundaries. ACP and UTF-8 are explicit.
#ifndef OPENATL_TEXT_ENCODING_H
#define OPENATL_TEXT_ENCODING_H
#include "platform.h"
#include <windows.h>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>

namespace OpenATL { namespace Unicode {

inline HRESULT ValidateUtf16(std::wstring_view input) noexcept {
    static_assert(sizeof(wchar_t) == 2, "OpenATL uses Windows UTF-16 WCHAR");
    if (!input.data() && !input.empty()) return E_INVALIDARG;
    for (size_t i = 0; i < input.size(); ++i) {
        const unsigned unit = static_cast<unsigned short>(input[i]);
        if (unit >= 0xdc00 && unit <= 0xdfff)
            return HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION);
        if (unit >= 0xd800 && unit <= 0xdbff) {
            if (++i == input.size()) return HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION);
            const unsigned low = static_cast<unsigned short>(input[i]);
            if (low < 0xdc00 || low > 0xdfff)
                return HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION);
        }
    }
    return S_OK;
}

// Input is borrowed for this synchronous call. Explicit lengths preserve NULs.
// Output is replaced only on success, including for empty input. Windows must
// support strict flags for the selected code page; there is no lossy fallback.
inline HRESULT CodePageToUtf16(UINT codePage, std::string_view input,
                               std::wstring& output) noexcept {
    if (!input.data() && !input.empty()) return E_INVALIDARG;
    if (input.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    try {
        std::wstring staged;
        if (!input.empty()) {
            const int count = static_cast<int>(input.size());
            const int required = ::MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS,
                                                       input.data(), count, nullptr, 0);
            if (!required) return HRESULT_FROM_WIN32(::GetLastError());
            staged.resize(static_cast<size_t>(required));
            const int written = ::MultiByteToWideChar(codePage, MB_ERR_INVALID_CHARS,
                                                       input.data(), count, staged.data(), required);
            if (!written) return HRESULT_FROM_WIN32(::GetLastError());
            if (written != required) return E_UNEXPECTED;
            HRESULT hr = ValidateUtf16(staged);
            if (FAILED(hr)) return hr;
        }
        output.swap(staged);
        return S_OK;
    } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
      catch (const std::length_error&) { return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW); }
}

inline HRESULT Utf16ToCodePage(UINT codePage, std::wstring_view input,
                               std::string& output) noexcept {
    if (input.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    HRESULT hr = ValidateUtf16(input);
    if (FAILED(hr)) return hr;
    // Resolve ACP before choosing flags: CP_ACP may be UTF-8 on this host.
    if (codePage == CP_ACP) codePage = ::GetACP();
    const bool unicodePage = codePage == CP_UTF8 || codePage == 54936u;
    const DWORD flags = unicodePage ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS;
    try {
        std::string staged;
        if (!input.empty()) {
            const int count = static_cast<int>(input.size());
            BOOL substituted = FALSE;
            LPBOOL usedDefault = unicodePage ? nullptr : &substituted;
            const int required = ::WideCharToMultiByte(codePage, flags, input.data(), count,
                                                       nullptr, 0, nullptr, usedDefault);
            if (!required) return HRESULT_FROM_WIN32(::GetLastError());
            if (substituted) return HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION);
            staged.resize(static_cast<size_t>(required));
            substituted = FALSE;
            const int written = ::WideCharToMultiByte(codePage, flags, input.data(), count,
                                                       staged.data(), required, nullptr, usedDefault);
            if (!written) return HRESULT_FROM_WIN32(::GetLastError());
            if (substituted) return HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION);
            if (written != required) return E_UNEXPECTED;
        }
        output.swap(staged);
        return S_OK;
    } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
      catch (const std::length_error&) { return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW); }
}

inline HRESULT Utf8ToUtf16(std::string_view input, std::wstring& output) noexcept {
    return CodePageToUtf16(CP_UTF8, input, output);
}
inline HRESULT Utf16ToUtf8(std::wstring_view input, std::string& output) noexcept {
    return Utf16ToCodePage(CP_UTF8, input, output);
}
inline HRESULT AnsiToUtf16(std::string_view input, std::wstring& output) noexcept {
    return CodePageToUtf16(::GetACP(), input, output);
}
inline HRESULT Utf16ToAnsi(std::wstring_view input, std::string& output) noexcept {
    return Utf16ToCodePage(::GetACP(), input, output);
}

}} // namespace OpenATL::Unicode
#endif
