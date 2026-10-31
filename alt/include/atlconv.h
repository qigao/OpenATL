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
#include "openatl/text_encoding.h"

#if !defined(UNICODE) || !defined(_UNICODE)
#error "OpenATL requires UNICODE/_UNICODE; MBCS is unsupported"
#endif

namespace ATL {
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
#define CW2A(x)     _openatlConversionScope.ToAnsi(x)
#define T2A(x)      _openatlConversionScope.ToAnsi(x)
#define T2CA(x)     static_cast<LPCSTR>(_openatlConversionScope.ToAnsi(x))
#define A2T(x)      _openatlConversionScope.ToWide(x)
#define A2CT(x)     static_cast<LPCTSTR>(_openatlConversionScope.ToWide(x))
#define A2OLE(x)    _openatlConversionScope.ToWide(x)
#define OLE2A(x)    _openatlConversionScope.ToAnsi(x)
#define A2COLE(x)   _openatlConversionScope.ToWide(x)
#define OLE2CA(x)   _openatlConversionScope.ToAnsi(x)

#endif // __ATLCONV_H__
