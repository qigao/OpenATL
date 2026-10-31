// OpenATL - Unicode-only conversion macros with real Win32 encoding.
// Each USES_CONVERSION scope owns returned buffers until lexical exit.
#ifndef __ATLCONV_H__
#define __ATLCONV_H__
#pragma once

#include <windows.h>
#include <tchar.h>
#include <list>
#include <string>
#include <utility>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <limits>
#include <exception>
#include "atldef.h"
#include "openatl/text_encoding.h"

#if !defined(UNICODE) || !defined(_UNICODE)
#error "OpenATL requires UNICODE/_UNICODE; MBCS is unsupported"
#endif

namespace ATL {
namespace detail {
[[noreturn]] inline void ConversionFailure(HRESULT hr) {
    AtlThrow(hr);
    std::terminate(); // Enforce failure if no-exception AtlThrow returns.
}
inline HRESULT ConversionWin32Error() noexcept {
    const DWORD error = ::GetLastError();
    return error ? HRESULT_FROM_WIN32(error) : E_UNEXPECTED;
}
inline UINT ConversionCodePage(UINT page) {
    // Resolve special identifiers before selecting UTF-8/GB18030 flags.
    // In particular, CP_THREAD_ACP must not silently become process ACP.
    CPINFOEXW info{};
    if (!::GetCPInfoExW(page, 0, &info))
        ConversionFailure(ConversionWin32Error());
    return info.CodePage;
}
template<class Character, int Count>
class ConversionBuffer {
    static_assert(Count > 0, "ATL conversion buffer must have positive capacity");
public:
    Character* m_psz = nullptr;
    Character m_szBuffer[Count]{};
    ConversionBuffer(const ConversionBuffer&) = delete;
    ConversionBuffer& operator=(const ConversionBuffer&) = delete;
protected:
    ConversionBuffer() noexcept = default;
    ~ConversionBuffer() noexcept { std::free(allocated_); }
    Character* Reserve(int count) {
        if (count <= Count) return m_szBuffer;
        if (static_cast<size_t>(count) >
            (std::numeric_limits<size_t>::max)() / sizeof(Character))
            ConversionFailure(E_OUTOFMEMORY);
        allocated_ = static_cast<Character*>(
            std::malloc(static_cast<size_t>(count) * sizeof(Character)));
        if (!allocated_) ConversionFailure(E_OUTOFMEMORY);
        return allocated_;
    }
private:
    Character* allocated_ = nullptr;
};
} // namespace detail

// Standard ATL object-owned conversion spelling. Small results use the
// public inline buffer; larger results are freed with the conversion object.
// Failed strict conversion throws CAtlException; legacy A2W/W2A below retain
// their existing nullptr + GetLastError contract and lexical scope owner.
template<int t_nBufferLength = 128>
class CA2WEX : public detail::ConversionBuffer<wchar_t, t_nBufferLength> {
public:
    CA2WEX(LPCSTR source) : CA2WEX(source, CP_THREAD_ACP) {}
    CA2WEX(LPCSTR source, UINT codePage) {
        if (!source) return;
        codePage = detail::ConversionCodePage(codePage);
        const int count = ::MultiByteToWideChar(
            codePage, MB_ERR_INVALID_CHARS, source, -1, nullptr, 0);
        if (!count) detail::ConversionFailure(detail::ConversionWin32Error());
        wchar_t* buffer = this->Reserve(count);
        const int written = ::MultiByteToWideChar(
            codePage, MB_ERR_INVALID_CHARS, source, -1, buffer, count);
        if (!written) detail::ConversionFailure(detail::ConversionWin32Error());
        if (written != count) detail::ConversionFailure(E_UNEXPECTED);
        this->m_psz = buffer;
    }
    operator LPWSTR() const noexcept { return this->m_psz; }
};

template<int t_nBufferLength = 128>
class CW2AEX : public detail::ConversionBuffer<char, t_nBufferLength> {
public:
    CW2AEX(LPCWSTR source) : CW2AEX(source, CP_THREAD_ACP) {}
    CW2AEX(LPCWSTR source, UINT codePage) {
        if (!source) return;
        const HRESULT valid = OpenATL::Unicode::ValidateUtf16(source);
        if (FAILED(valid)) detail::ConversionFailure(valid);
        codePage = detail::ConversionCodePage(codePage);
        const bool unicodePage = codePage == CP_UTF8 || codePage == 54936u;
        const DWORD flags = unicodePage ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS;
        BOOL substituted = FALSE;
        LPBOOL usedDefault = unicodePage ? nullptr : &substituted;
        const int count = ::WideCharToMultiByte(
            codePage, flags, source, -1, nullptr, 0, nullptr, usedDefault);
        if (!count) detail::ConversionFailure(detail::ConversionWin32Error());
        if (substituted)
            detail::ConversionFailure(HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION));
        char* buffer = this->Reserve(count);
        substituted = FALSE;
        const int written = ::WideCharToMultiByte(
            codePage, flags, source, -1, buffer, count, nullptr, usedDefault);
        if (!written) detail::ConversionFailure(detail::ConversionWin32Error());
        if (substituted)
            detail::ConversionFailure(HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION));
        if (written != count) detail::ConversionFailure(E_UNEXPECTED);
        this->m_psz = buffer;
    }
    operator LPSTR() const noexcept { return this->m_psz; }
};

using CA2W = CA2WEX<>;
using CW2A = CW2AEX<>;
using CA2T = CA2W;
using CT2A = CW2A;
template<int Count = 128> using CA2TEX = CA2WEX<Count>;
template<int Count = 128> using CT2AEX = CW2AEX<Count>;

class CConversionScope final {
public:
    CConversionScope() = default;
    CConversionScope(const CConversionScope&) = delete;
    CConversionScope& operator=(const CConversionScope&) = delete;

    LPWSTR ToWide(LPCSTR text) { return StoreWide(::GetACP(), text); }
    LPSTR ToAnsi(LPCWSTR text) { return StoreNarrow(::GetACP(), text); }
    LPWSTR ToWideUtf8(LPCSTR text) { return StoreWide(CP_UTF8, text); }
    LPSTR ToUtf8(LPCWSTR text) { return StoreNarrow(CP_UTF8, text); }
private:
    static void ConversionError(HRESULT hr) noexcept {
        ::SetLastError(HRESULT_FACILITY(hr) == FACILITY_WIN32 ?
                       HRESULT_CODE(hr) : ERROR_INVALID_DATA);
    }
    LPWSTR StoreWide(UINT codePage, LPCSTR text) {
        if (!text) return nullptr;
        std::wstring converted;
        HRESULT hr = OpenATL::Unicode::CodePageToUtf16(codePage, text, converted);
        if (FAILED(hr)) { ConversionError(hr); return nullptr; }
        try {
            wide_.push_back(std::move(converted));
            return wide_.back().data();
        } catch (const std::bad_alloc&) {
            ConversionError(E_OUTOFMEMORY); return nullptr;
        }
    }
    LPSTR StoreNarrow(UINT codePage, LPCWSTR text) {
        if (!text) return nullptr;
        std::string converted;
        HRESULT hr = OpenATL::Unicode::Utf16ToCodePage(codePage, text, converted);
        if (FAILED(hr)) { ConversionError(hr); return nullptr; }
        try {
            ansi_.push_back(std::move(converted));
            return ansi_.back().data();
        } catch (const std::bad_alloc&) {
            ConversionError(E_OUTOFMEMORY); return nullptr;
        }
    }
    // list elements and their character buffers stay stable when a second
    // conversion occurs. No process-global, TLS or provider fallback state.
    std::list<std::wstring> wide_;
    std::list<std::string> ansi_;
};
} // namespace ATL

// ANSI conversion macros must be paired with one explicit lexical owner.
#define USES_CONVERSION ATL::CConversionScope _openatlConversionScope

// UNICODE TCHAR, WCHAR and OLECHAR have identical representation.
#define T2W(x)      (x)
#define W2T(x)      (x)
#define W2CT(x)     static_cast<LPCTSTR>(x)
#define T2CW(x)     (x)
#define CW2T(x)     (x)
#define T2OLE(x)    (x)
#define OLE2T(x)    (x)
#define T2COLE(x)   (x)
#define OLE2CT(x)   (x)
#define CT2OLE(x)   (x)
#define CT2COLE(x)  (x)
#define W2OLE(x)    (x)
#define OLE2W(x)    (x)
#define W2COLE(x)   (x)
#define OLE2CW(x)   (x)

// Real ANSI code-page conversion. Never reinterpret ANSI bytes as WCHAR.
#define A2W(x)      _openatlConversionScope.ToWide(x)
#define W2A(x)      _openatlConversionScope.ToAnsi(x)
#define A2CW(x)     _openatlConversionScope.ToWide(x)
#define T2A(x)      _openatlConversionScope.ToAnsi(x)
#define T2CA(x)     static_cast<LPCSTR>(_openatlConversionScope.ToAnsi(x))
#define A2T(x)      _openatlConversionScope.ToWide(x)
#define A2CT(x)     static_cast<LPCTSTR>(_openatlConversionScope.ToWide(x))
#define A2OLE(x)    _openatlConversionScope.ToWide(x)
#define OLE2A(x)    _openatlConversionScope.ToAnsi(x)
#define A2COLE(x)   _openatlConversionScope.ToWide(x)
#define OLE2CA(x)   _openatlConversionScope.ToAnsi(x)

#endif // __ATLCONV_H__
