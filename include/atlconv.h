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

#if !defined(UNICODE) || !defined(_UNICODE)
#error "OpenATL requires UNICODE/_UNICODE; MBCS is unsupported"
#endif

namespace ATL {
class CConversionScope final {
public:
    CConversionScope() = default;
    CConversionScope(const CConversionScope&) = delete;
    CConversionScope& operator=(const CConversionScope&) = delete;

    LPWSTR ToWide(LPCSTR text) {
        if (text == nullptr) return nullptr;
        const int length = ::MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
        if (length <= 0) return nullptr;
        std::wstring converted(static_cast<size_t>(length), L'\0');
        if (::MultiByteToWideChar(CP_ACP, 0, text, -1, converted.data(), length)
              != length)
            return nullptr;
        converted.resize(static_cast<size_t>(length - 1));
        wide_.push_back(std::move(converted));
        return wide_.back().data();
    }
    LPSTR ToAnsi(LPCWSTR text) {
        if (text == nullptr) return nullptr;
        const int length = ::WideCharToMultiByte(CP_ACP, 0, text, -1, nullptr,
                                                  0, nullptr, nullptr);
        if (length <= 0) return nullptr;
        std::string converted(static_cast<size_t>(length), '\0');
        if (::WideCharToMultiByte(CP_ACP, 0, text, -1, converted.data(), length,
                                  nullptr, nullptr) != length)
            return nullptr;
        converted.resize(static_cast<size_t>(length - 1));
        ansi_.push_back(std::move(converted));
        return ansi_.back().data();
    }
private:
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
#define A2T(x)      _openatlConversionScope.ToWide(x)
#define A2OLE(x)    _openatlConversionScope.ToWide(x)
#define OLE2A(x)    _openatlConversionScope.ToAnsi(x)
#define A2COLE(x)   _openatlConversionScope.ToWide(x)
#define OLE2CA(x)   _openatlConversionScope.ToAnsi(x)

#endif // __ATLCONV_H__
