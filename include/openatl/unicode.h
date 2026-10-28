// Explicit Unicode boundaries, with optional Salts Unicode 17 text processing.
#ifndef OPENATL_UNICODE_H
#define OPENATL_UNICODE_H
#include "text_encoding.h"

#if defined(OPENATL_USE_SALTS_UNICODE) && OPENATL_USE_SALTS_UNICODE
#include <salts_unicode.h>
#include <salts_unicode_normalize.h>
#include <vector>

static_assert(SALTS_UNICODE_VERSION_MAJOR == 17 && SALTS_UNICODE_VERSION_MINOR == 0 &&
              SALTS_UNICODE_VERSION_PATCH == 0, "OpenATL text processing requires Unicode 17.0.0");

namespace OpenATL { namespace Unicode {
inline constexpr size_t DefaultTextLimit = 1024 * 1024;
namespace detail {
inline HRESULT textInput(std::string_view input, size_t limit) noexcept {
    if (!input.data() && !input.empty()) return E_INVALIDARG;
    return input.size() <= limit ? S_OK : HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
}
inline vstr view(std::string_view input) noexcept {
    return vstr_from_buf(input.data() ? input.data() : "", input.size());
}
inline HRESULT status(salts_unicode_status value) noexcept {
    switch (value) {
    case SALTS_UNICODE_OK: return S_OK;
    case SALTS_UNICODE_ERR_INVALID_UTF8: return HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION);
    case SALTS_UNICODE_ERR_INVALID_ARGUMENT: return E_INVALIDARG;
    case SALTS_UNICODE_ERR_CAPACITY: return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    default: return E_UNEXPECTED;
    }
}
} // namespace detail

// All operations borrow immutable input synchronously and publish owned output
// only after complete success. The limit counts input UTF-8 bytes; transforms
// are explicit and never change CString/BSTR comparisons or code-unit indexing.
inline HRESULT NormalizeNfcUtf8(std::string_view input, std::string& output,
                                size_t limit = DefaultTextLimit) noexcept {
    HRESULT hr = detail::textInput(input, limit);
    if (FAILED(hr)) return hr;
    if (input.size() > ((std::numeric_limits<size_t>::max)() - 1) / 16)
        return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
    try {
        // The Salts NFC contract bounds decomposition to four workspace
        // scalars per input byte; each output scalar needs at most four bytes.
        std::vector<uint32_t> workspace(input.size() * 4);
        std::string staged(input.size() * 16 + 1, '\0');
        size_t written = 0;
        const auto status = salts_unicode_nfc(detail::view(input), SALTS_UNICODE_NFC_17_0_0,
            workspace.data(), workspace.size(), staged.data(), staged.size(), &written);
        switch (status) {
        case SALTS_UNICODE_NFC_OK: break;
        case SALTS_UNICODE_NFC_INVALID_UTF8: return HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION);
        case SALTS_UNICODE_NFC_INVALID_ARGUMENT: return E_INVALIDARG;
        case SALTS_UNICODE_NFC_UNSUPPORTED_VERSION: return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
        case SALTS_UNICODE_NFC_OVERFLOW: return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
        case SALTS_UNICODE_NFC_WORKSPACE:
        case SALTS_UNICODE_NFC_OUTPUT_CAPACITY: return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
        default: return E_UNEXPECTED;
        }
        if (written >= staged.size()) return E_UNEXPECTED;
        staged.resize(written);
        output.swap(staged);
        return S_OK;
    } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
      catch (const std::length_error&) { return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW); }
}

inline HRESULT CaseFoldUtf8(std::string_view input, std::string& output,
                            size_t limit = DefaultTextLimit) noexcept {
    HRESULT hr = detail::textInput(input, limit);
    if (FAILED(hr)) return hr;
    size_t required = 0;
    hr = detail::status(salts_unicode_case_fold(detail::view(input), nullptr, 0, &required));
    if (FAILED(hr)) return hr;
    try {
        std::string staged(required, '\0');
        size_t written = 0;
        hr = detail::status(salts_unicode_case_fold(detail::view(input), staged.data(), staged.size(), &written));
        if (FAILED(hr)) return hr;
        if (written != required) return E_UNEXPECTED;
        output.swap(staged);
        return S_OK;
    } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
      catch (const std::length_error&) { return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW); }
}

inline HRESULT TrimUtf8(std::string_view input, std::string& output,
                        size_t limit = DefaultTextLimit) noexcept {
    HRESULT hr = detail::textInput(input, limit);
    if (FAILED(hr)) return hr;
    vstr trimmed{};
    hr = detail::status(salts_unicode_trim_whitespace(detail::view(input), &trimmed));
    if (FAILED(hr)) return hr;
    try {
        std::string staged;
        if (trimmed.len) staged.assign(trimmed.data, trimmed.len);
        output.swap(staged);
        return S_OK;
    } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
      catch (const std::length_error&) { return HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW); }
}

inline HRESULT NormalizeNfcUtf16(std::wstring_view input, std::wstring& output,
                                 size_t limit = DefaultTextLimit) noexcept {
    if (input.size() > limit) return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
    std::string utf8, normalized;
    HRESULT hr = Utf16ToUtf8(input, utf8);
    if (SUCCEEDED(hr)) hr = NormalizeNfcUtf8(utf8, normalized, limit);
    return FAILED(hr) ? hr : Utf8ToUtf16(normalized, output);
}
inline HRESULT CaseFoldUtf16(std::wstring_view input, std::wstring& output,
                             size_t limit = DefaultTextLimit) noexcept {
    if (input.size() > limit) return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
    std::string utf8, folded;
    HRESULT hr = Utf16ToUtf8(input, utf8);
    if (SUCCEEDED(hr)) hr = CaseFoldUtf8(utf8, folded, limit);
    return FAILED(hr) ? hr : Utf8ToUtf16(folded, output);
}
}} // namespace OpenATL::Unicode
#endif
#endif
