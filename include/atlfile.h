// OpenATL - CAtlFile, a small Win32 file-owner convenience.
// Independently authored for this project under Apache-2.0.
// Compatible with a bounded subset of ATL CAtlFile's public contract.
// No COM, RATL, registry, native persistence layer or server dependency.
#ifndef OPENATL_ATLFILE_H
#define OPENATL_ATLFILE_H
#pragma once

#include "atlbase.h"
#include <limits>
#include <utility>

namespace ATL {

class CAtlFile : public CHandle {
public:
    CAtlFile() noexcept = default;
    explicit CAtlFile(HANDLE h) noexcept : CHandle(h) {}
    CAtlFile(const CAtlFile&) = delete;
    CAtlFile& operator=(const CAtlFile&) = delete;
    CAtlFile(CAtlFile&& other) noexcept : CHandle(std::move(other)) {}
    CAtlFile& operator=(CAtlFile&& other) noexcept {
        CHandle::operator=(std::move(other));
        return *this;
    }

    bool IsOpen() const noexcept {
        return m_h != NULL && m_h != INVALID_HANDLE_VALUE;
    }

    HRESULT Create(LPCTSTR path, DWORD access, DWORD shareMode,
                   DWORD creationDisposition,
                   DWORD flagsAndAttributes = FILE_ATTRIBUTE_NORMAL,
                   LPSECURITY_ATTRIBUTES security = NULL) noexcept {
        if (!path || !*path)
            return E_INVALIDARG;
        if (IsOpen())
            return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
        HANDLE h = ::CreateFile(path, access, shareMode, security,
                                creationDisposition, flagsAndAttributes, NULL);
        if (h == INVALID_HANDLE_VALUE)
            return HRESULT_FROM_WIN32(::GetLastError());
        Attach(h);
        return S_OK;
    }

    HRESULT Read(void* buffer, DWORD length, DWORD* received = NULL) noexcept {
        if (received) *received = 0;
        if (!IsOpen()) return E_HANDLE;
        if (length && !buffer) return E_POINTER;
        if (!length) return S_OK;
        DWORD actual = 0;
        if (!::ReadFile(m_h, buffer, length, &actual, NULL))
            return HRESULT_FROM_WIN32(::GetLastError());
        if (received) *received = actual;
        return S_OK; // EOF is S_OK with zero bytes
    }

    HRESULT Write(const void* buffer, DWORD length, DWORD* sent = NULL) noexcept {
        if (sent) *sent = 0;
        if (!IsOpen()) return E_HANDLE;
        if (length && !buffer) return E_POINTER;
        if (!length) return S_OK;
        const BYTE* bytes = static_cast<const BYTE*>(buffer);
        DWORD total = 0;
        while (total < length) {
            DWORD now = 0;
            if (!::WriteFile(m_h, bytes + total, length - total, &now, NULL)) {
                if (sent) *sent = total;
                return HRESULT_FROM_WIN32(::GetLastError());
            }
            if (!now) {
                if (sent) *sent = total;
                return HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
            }
            total += now;
        }
        if (sent) *sent = total;
        return S_OK;
    }

    HRESULT Seek(LONGLONG offset, DWORD origin,
                 ULONGLONG* position = NULL) noexcept {
        if (position) *position = 0;
        if (!IsOpen()) return E_HANDLE;
        if (origin != FILE_BEGIN && origin != FILE_CURRENT && origin != FILE_END)
            return E_INVALIDARG;
        LARGE_INTEGER delta = {}, result = {};
        delta.QuadPart = offset;
        if (!::SetFilePointerEx(m_h, delta, &result, origin))
            return HRESULT_FROM_WIN32(::GetLastError());
        if (position) *position = static_cast<ULONGLONG>(result.QuadPart);
        return S_OK;
    }

    HRESULT GetSize(ULONGLONG* size) const noexcept {
        if (!size) return E_POINTER;
        *size = 0;
        if (!IsOpen()) return E_HANDLE;
        LARGE_INTEGER actual = {};
        if (!::GetFileSizeEx(m_h, &actual))
            return HRESULT_FROM_WIN32(::GetLastError());
        *size = static_cast<ULONGLONG>(actual.QuadPart);
        return S_OK;
    }

    HRESULT SetSize(ULONGLONG size) noexcept {
        if (!IsOpen()) return E_HANDLE;
        if (size > static_cast<ULONGLONG>(
                (std::numeric_limits<LONGLONG>::max)()))
            return E_INVALIDARG;
        HRESULT hr = Seek(static_cast<LONGLONG>(size), FILE_BEGIN);
        if (FAILED(hr)) return hr;
        return ::SetEndOfFile(m_h) ? S_OK :
            HRESULT_FROM_WIN32(::GetLastError());
    }

    HRESULT Flush() noexcept {
        if (!IsOpen()) return E_HANDLE;
        return ::FlushFileBuffers(m_h) ? S_OK :
            HRESULT_FROM_WIN32(::GetLastError());
    }

    HRESULT Close() noexcept {
        if (!IsOpen()) return S_OK;
        if (!::CloseHandle(m_h))
            return HRESULT_FROM_WIN32(::GetLastError());
        m_h = NULL;
        return S_OK;
    }
};

} // namespace ATL
#endif // OPENATL_ATLFILE_H
