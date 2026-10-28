// OpenATL - source-owned Win32 registry key handle and value utilities.
// This is for ordinary application settings, NOT COM registration or RGS.
// No COM apartment, class factory, registrar, or external ATL is required.
#ifndef __ATLREG_H__
#define __ATLREG_H__

#pragma once

#include "openatl/platform.h"
#include <windows.h>
#include <tchar.h>
#include <limits.h>

namespace ATL {

class CRegKey {
public:
    HKEY m_hKey;

    CRegKey() noexcept : m_hKey(nullptr) {}
    explicit CRegKey(HKEY key) noexcept : m_hKey(key) {}
    CRegKey(CRegKey&& other) noexcept : m_hKey(other.Detach()) {}
    CRegKey(const CRegKey&) = delete;
    CRegKey& operator=(const CRegKey&) = delete;

    CRegKey& operator=(CRegKey&& other) noexcept {
        if (this != &other && Close() == ERROR_SUCCESS)
            m_hKey = other.Detach();
        return *this;
    }

    ~CRegKey() { (void)Close(); }

    operator HKEY() const noexcept { return m_hKey; }

    // Failed close leaves the original handle owned, so a later close may retry.
    LSTATUS Close() noexcept {
        if (m_hKey == nullptr)
            return ERROR_SUCCESS;
        LSTATUS result = ::RegCloseKey(m_hKey);
        if (result == ERROR_SUCCESS)
            m_hKey = nullptr;
        return result;
    }

    HKEY Detach() noexcept {
        HKEY key = m_hKey;
        m_hKey = nullptr;
        return key;
    }

    // Caller transfers ownership only on successful Attach.
    LSTATUS Attach(HKEY key) noexcept {
        if (key == m_hKey)
            return ERROR_SUCCESS;
        LSTATUS result = Close();
        if (result == ERROR_SUCCESS)
            m_hKey = key;
        return result;
    }

    // Replace an owned handle only after a successful Win32 open AND close.
    LSTATUS Open(HKEY parent, LPCTSTR subkey, REGSAM access = KEY_READ) noexcept {
        if (parent == nullptr || subkey == nullptr || *subkey == 0)
            return ERROR_INVALID_PARAMETER;
        HKEY next = nullptr;
        LSTATUS result = ::RegOpenKeyEx(parent, subkey, 0, access, &next);
        if (result != ERROR_SUCCESS)
            return result;
        result = Close();
        if (result == ERROR_SUCCESS)
            m_hKey = next;
        else
            (void)::RegCloseKey(next);
        return result;
    }

    LSTATUS Create(HKEY parent, LPCTSTR subkey, LPTSTR keyClass = nullptr,
                   DWORD options = REG_OPTION_NON_VOLATILE,
                   REGSAM access = KEY_READ | KEY_WRITE,
                   LPSECURITY_ATTRIBUTES security = nullptr,
                   LPDWORD disposition = nullptr) noexcept {
        if (parent == nullptr || subkey == nullptr || *subkey == 0)
            return ERROR_INVALID_PARAMETER;
        HKEY next = nullptr;
        DWORD created = 0;
        LSTATUS result = ::RegCreateKeyEx(parent, subkey, 0, keyClass,
                                           options, access, security,
                                           &next, &created);
        if (result != ERROR_SUCCESS)
            return result;
        result = Close();
        if (result != ERROR_SUCCESS) {
            (void)::RegCloseKey(next);
            return result;
        }
        m_hKey = next;
        if (disposition != nullptr)
            *disposition = created;
        return ERROR_SUCCESS;
    }

    LSTATUS QueryDWORDValue(LPCTSTR valueName, DWORD& result) const noexcept {
        if (m_hKey == nullptr)
            return ERROR_INVALID_HANDLE;
        DWORD type = 0, bytes = 0;
        LSTATUS status = ::RegQueryValueEx(m_hKey, valueName, nullptr,
                                           &type, nullptr, &bytes);
        if (status != ERROR_SUCCESS)
            return status;
        if (type != REG_DWORD || bytes != sizeof(DWORD))
            return ERROR_INVALID_DATATYPE;
        DWORD staged = 0;
        status = ::RegQueryValueEx(m_hKey, valueName, nullptr, &type,
                                   reinterpret_cast<LPBYTE>(&staged), &bytes);
        if (status != ERROR_SUCCESS)
            return status;
        if (type != REG_DWORD || bytes != sizeof(DWORD))
            return ERROR_INVALID_DATATYPE;
        result = staged;
        return ERROR_SUCCESS;
    }

    LSTATUS SetDWORDValue(LPCTSTR valueName, DWORD value) const noexcept {
        if (m_hKey == nullptr)
            return ERROR_INVALID_HANDLE;
        return ::RegSetValueEx(m_hKey, valueName, 0, REG_DWORD,
                               reinterpret_cast<const BYTE*>(&value), sizeof(value));
    }

    // In/out character capacity includes the NUL. A null destination queries
    // required capacity only. REG_EXPAND_SZ is returned verbatim, not expanded.
    // RegGetValue handles malformed REG_SZ lacking the trailing NUL; its
    // ZEROONFAILURE flag prevents callers from using unterminated partial data.
    LSTATUS QueryStringValue(LPCTSTR valueName, LPTSTR out,
                             ULONG* characters) const noexcept {
        if (characters == nullptr)
            return ERROR_INVALID_PARAMETER;
        if (m_hKey == nullptr)
            return ERROR_INVALID_HANDLE;
        if (out != nullptr && *characters > MAXDWORD / sizeof(TCHAR))
            return ERROR_INVALID_PARAMETER;

        DWORD size = (out != nullptr) ? *characters * sizeof(TCHAR) : 0;
        constexpr DWORD flags = RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ |
                                RRF_NOEXPAND | RRF_ZEROONFAILURE;
        const LSTATUS status = ::RegGetValue(m_hKey, nullptr, valueName, flags,
                                              nullptr, out, &size);
        // Preserve ATL's documented invalid-type response, while using the
        // Win32 API that guarantees terminating NUL for REG_SZ/REG_EXPAND_SZ.
        if (status == ERROR_UNSUPPORTED_TYPE)
            return ERROR_INVALID_DATATYPE;
        if (status == ERROR_SUCCESS || status == ERROR_MORE_DATA) {
            if (size % sizeof(TCHAR) != 0)
                return ERROR_INVALID_DATA;
            // RegGetValue provides the required byte count, including
            // sufficient terminator space. The exact count may exceed the
            // logical string length; pass it through rather than guessing.
            *characters = size / sizeof(TCHAR);
        }
        return status;
    }

    LSTATUS SetStringValue(LPCTSTR valueName, LPCTSTR value,
                           DWORD type = REG_SZ) const noexcept {
        if (m_hKey == nullptr)
            return ERROR_INVALID_HANDLE;
        if (value == nullptr || (type != REG_SZ && type != REG_EXPAND_SZ))
            return ERROR_INVALID_PARAMETER;
        const size_t count = _tcslen(value);
        if (count >= static_cast<size_t>(MAXDWORD / sizeof(TCHAR)))
            return ERROR_INVALID_PARAMETER;
        DWORD bytes = static_cast<DWORD>((count + 1) * sizeof(TCHAR));
        return ::RegSetValueEx(m_hKey, valueName, 0, type,
                               reinterpret_cast<const BYTE*>(value), bytes);
    }

    LSTATUS QueryBinaryValue(LPCTSTR valueName, void* out,
                             ULONG* bytes) const noexcept {
        if (bytes == nullptr)
            return ERROR_INVALID_PARAMETER;
        if (m_hKey == nullptr)
            return ERROR_INVALID_HANDLE;
        DWORD type = 0, needed = 0;
        LSTATUS status = ::RegQueryValueEx(m_hKey, valueName, nullptr,
                                           &type, nullptr, &needed);
        if (status != ERROR_SUCCESS)
            return status;
        if (type != REG_BINARY)
            return ERROR_INVALID_DATATYPE;
        if (out == nullptr) {
            *bytes = needed;
            return ERROR_SUCCESS;
        }
        if (*bytes < needed) {
            *bytes = needed;
            return ERROR_MORE_DATA;
        }
        status = ::RegQueryValueEx(m_hKey, valueName, nullptr,
                                   &type, static_cast<LPBYTE>(out), bytes);
        return status == ERROR_SUCCESS && type != REG_BINARY
               ? ERROR_INVALID_DATATYPE : status;
    }

    LSTATUS SetBinaryValue(LPCTSTR valueName, const void* value,
                           ULONG bytes) const noexcept {
        if (m_hKey == nullptr)
            return ERROR_INVALID_HANDLE;
        if (value == nullptr && bytes != 0)
            return ERROR_INVALID_PARAMETER;
        return ::RegSetValueEx(m_hKey, valueName, 0, REG_BINARY,
                               static_cast<const BYTE*>(value), bytes);
    }

    LSTATUS DeleteValue(LPCTSTR valueName) const noexcept {
        if (m_hKey == nullptr)
            return ERROR_INVALID_HANDLE;
        return ::RegDeleteValue(m_hKey, valueName);
    }

    // Deliberately nonrecursive; no RGS/COM registrar or registration helper.
    LSTATUS DeleteSubKey(LPCTSTR subkey) const noexcept {
        if (m_hKey == nullptr)
            return ERROR_INVALID_HANDLE;
        if (subkey == nullptr || *subkey == 0)
            return ERROR_INVALID_PARAMETER;
        return ::RegDeleteKey(m_hKey, subkey);
    }
};

} // namespace ATL

#endif // __ATLREG_H__
