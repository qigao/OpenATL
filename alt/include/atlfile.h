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


// FILETIME mapping is not COM or a native persistence provider: this class
// simply owns one Win32 section handle and one mapped view. Inspired by the
// ATL/RATL public API, implemented independently with strict failure cleanup.
class CAtlFileMappingBase {
public:
    CAtlFileMappingBase() noexcept = default;
    ~CAtlFileMappingBase() noexcept { (void)Unmap(); }

    CAtlFileMappingBase(const CAtlFileMappingBase&) = delete;
    CAtlFileMappingBase& operator=(const CAtlFileMappingBase&) = delete;
    CAtlFileMappingBase(CAtlFileMappingBase&& other) noexcept {
        MoveFrom(other);
    }
    CAtlFileMappingBase& operator=(CAtlFileMappingBase&&) = delete;

    // Explicitly copy the mapping, not its view pointer or owned HANDLE.
    // The destination must be empty; failure never changes its ownership.
    HRESULT CopyFrom(const CAtlFileMappingBase& other) noexcept {
        if (&other == this) return S_OK;
        if (m_mapping != NULL) return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
        if (other.m_mapping == NULL || other.m_base == NULL) return E_HANDLE;
        HANDLE duplicate = NULL;
        if (!::DuplicateHandle(::GetCurrentProcess(), other.m_mapping,
                               ::GetCurrentProcess(), &duplicate, 0, FALSE,
                               DUPLICATE_SAME_ACCESS))
            return HRESULT_FROM_WIN32(::GetLastError());
        return MapOwned(duplicate, other.m_length, other.m_offset, other.m_access);
    }

    // Maps a nonempty region within an existing regular file. A zero size
    // means offset-to-EOF. Neither writes beyond EOF nor resizes the file.
    // Unaligned offsets are handled by mapping from the previous Win32
    // allocation-granularity boundary and exposing the requested subrange.
    HRESULT MapFile(HANDLE file, SIZE_T length = 0, ULONGLONG offset = 0,
                    DWORD protection = PAGE_READONLY,
                    DWORD access = FILE_MAP_READ) noexcept {
        if (m_mapping != NULL) return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
        if (file == NULL || file == INVALID_HANDLE_VALUE) return E_HANDLE;
        LARGE_INTEGER size{};
        if (!::GetFileSizeEx(file, &size))
            return HRESULT_FROM_WIN32(::GetLastError());
        if (size.QuadPart <= 0 || offset >= static_cast<ULONGLONG>(size.QuadPart))
            return E_INVALIDARG;
        const ULONGLONG remaining = static_cast<ULONGLONG>(size.QuadPart) - offset;
        if (length == 0) {
            if (remaining > static_cast<ULONGLONG>((std::numeric_limits<SIZE_T>::max)()))
                return E_INVALIDARG;
            length = static_cast<SIZE_T>(remaining);
        } else if (static_cast<ULONGLONG>(length) > remaining) {
            return E_INVALIDARG;
        }
        HANDLE mapping = ::CreateFileMapping(file, NULL, protection, 0, 0, NULL);
        if (mapping == NULL) return HRESULT_FROM_WIN32(::GetLastError());
        return MapOwned(mapping, length, offset, access);
    }

    // Named shared memory with explicit size and optional "already exists"
    // report. Never performs registry writes or COM server registration.
    HRESULT MapSharedMem(SIZE_T length, LPCTSTR name,
                         BOOL* alreadyExisted = NULL,
                         LPSECURITY_ATTRIBUTES security = NULL,
                         DWORD protection = PAGE_READWRITE,
                         DWORD access = FILE_MAP_ALL_ACCESS) noexcept {
        if (alreadyExisted != NULL) *alreadyExisted = FALSE;
        if (m_mapping != NULL) return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
        if (name == NULL || *name == 0 || length == 0)
            return E_INVALIDARG;
        const ULONGLONG bytes = static_cast<ULONGLONG>(length);
        HANDLE mapping = ::CreateFileMapping(INVALID_HANDLE_VALUE, security,
            protection, static_cast<DWORD>(bytes >> 32),
            static_cast<DWORD>(bytes), name);
        if (mapping == NULL) return HRESULT_FROM_WIN32(::GetLastError());
        const BOOL exists = (::GetLastError() == ERROR_ALREADY_EXISTS);
        const HRESULT hr = MapOwned(mapping, length, 0, access);
        if (SUCCEEDED(hr) && alreadyExisted != NULL) *alreadyExisted = exists;
        return hr;
    }

    HRESULT OpenMapping(LPCTSTR name, SIZE_T length, ULONGLONG offset = 0,
                        DWORD access = FILE_MAP_ALL_ACCESS) noexcept {
        if (m_mapping != NULL) return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
        if (name == NULL || *name == 0 || length == 0)
            return E_INVALIDARG;
        HANDLE mapping = ::OpenFileMapping(access, FALSE, name);
        if (mapping == NULL) return HRESULT_FROM_WIN32(::GetLastError());
        return MapOwned(mapping, length, offset, access);
    }

    // Preserve owned handles on a failed Win32 cleanup so callers can retry.
    HRESULT Unmap() noexcept {
        if (m_base != NULL) {
            if (!::UnmapViewOfFile(m_base))
                return HRESULT_FROM_WIN32(::GetLastError());
            m_base = NULL;
            m_data = NULL;
            m_length = 0;
            m_offset = 0;
            m_access = 0;
        }
        if (m_mapping != NULL) {
            if (!::CloseHandle(m_mapping))
                return HRESULT_FROM_WIN32(::GetLastError());
            m_mapping = NULL;
        }
        return S_OK;
    }

    HRESULT Flush() noexcept {
        if (m_base == NULL) return E_HANDLE;
        return ::FlushViewOfFile(m_base, 0) ? S_OK :
            HRESULT_FROM_WIN32(::GetLastError());
    }

    void* GetData() const noexcept { return m_data; }
    HANDLE GetHandle() const noexcept { return m_mapping; }
    SIZE_T GetMappingSize() const noexcept { return m_length; }

private:
    // Takes ownership of the supplied mapping HANDLE even on failure.
    HRESULT MapOwned(HANDLE owned, SIZE_T length, ULONGLONG offset,
                     DWORD access) noexcept {
        if (owned == NULL) return E_HANDLE;
        if (length == 0 || offset > (std::numeric_limits<ULONGLONG>::max)() -
                                     static_cast<ULONGLONG>(length)) {
            ::CloseHandle(owned);
            return E_INVALIDARG;
        }
        SYSTEM_INFO systemInfo{};
        ::GetSystemInfo(&systemInfo);
        const DWORD granularity = systemInfo.dwAllocationGranularity;
        if (granularity == 0) {
            ::CloseHandle(owned);
            return E_FAIL;
        }
        const ULONGLONG aligned = offset - offset % granularity;
        const ULONGLONG delta = offset - aligned;
        if (delta > (std::numeric_limits<SIZE_T>::max)() - length) {
            ::CloseHandle(owned);
            return E_INVALIDARG;
        }
        const SIZE_T span = length + static_cast<SIZE_T>(delta);
        void* base = ::MapViewOfFile(owned, access,
            static_cast<DWORD>(aligned >> 32), static_cast<DWORD>(aligned), span);
        if (base == NULL) {
            const DWORD error = ::GetLastError();
            ::CloseHandle(owned);
            return HRESULT_FROM_WIN32(error);
        }
        m_mapping = owned;
        m_base = base;
        m_data = static_cast<BYTE*>(base) + static_cast<SIZE_T>(delta);
        m_length = length;
        m_offset = offset;
        m_access = access;
        return S_OK;
    }

    void MoveFrom(CAtlFileMappingBase& other) noexcept {
        m_mapping = other.m_mapping;
        m_base = other.m_base;
        m_data = other.m_data;
        m_length = other.m_length;
        m_offset = other.m_offset;
        m_access = other.m_access;
        other.m_mapping = NULL;
        other.m_base = NULL;
        other.m_data = NULL;
        other.m_length = 0;
        other.m_offset = 0;
        other.m_access = 0;
    }

    HANDLE m_mapping = NULL;
    void* m_base = NULL;
    BYTE* m_data = NULL;
    SIZE_T m_length = 0;
    ULONGLONG m_offset = 0;
    DWORD m_access = 0;
};

template <typename T = char>
class CAtlFileMapping : public CAtlFileMappingBase {
public:
    operator T*() const noexcept {
        return static_cast<T*>(GetData());
    }
};

} // namespace ATL
#endif // OPENATL_ATLFILE_H
