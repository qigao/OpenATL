// OpenATL - source-owned Win32 registry key handle and value utilities.
// This is for ordinary application settings, NOT COM registration or RGS.
// No COM apartment, class factory, registrar, or external ATL is required.
#ifndef __ATLREG_H__
#define __ATLREG_H__

#pragma once

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
    // required capacity only. Nonterminated REG_SZ is terminated if space allows.
    LSTATUS QueryStringValue(LPCTSTR valueName, LPTSTR out,
                             ULONG* characters) const noexcept {
        if (characters == nullptr)
            return ERROR_INVALID_PARAMETER;
        if (m_hKey == nullptr)
            return ERROR_INVALID_HANDLE;
        DWORD type = 0, bytes = 0;
        LSTATUS status = ::RegQueryValueEx(m_hKey, valueName, nullptr,
                                           &type, nullptr, &bytes);
        if (status != ERROR_SUCCESS)
            return status;
        if (type != REG_SZ && type != REG_EXPAND_SZ)
            return ERROR_INVALID_DATATYPE;
        if (bytes % sizeof(TCHAR) != 0)
            return ERROR_INVALID_DATA;
        ULONG needed = bytes / sizeof(TCHAR);
        if (needed == 0)
            needed = 1;
        if (out == nullptr) {
            *characters = needed + (bytes ? 1u : 0u);
            return ERROR_SUCCESS;
        }
        ULONG capacity = *characters;
        if (capacity < needed) {
            *characters = needed;
            return ERROR_MORE_DATA;
        }
        if (capacity > MAXDWORD / sizeof(TCHAR))
            return ERROR_INVALID_PARAMETER;
        DWORD actualBytes = capacity * sizeof(TCHAR);
        status = ::RegQueryValueEx(m_hKey, valueName, nullptr,
                                   &type, reinterpret_cast<LPBYTE>(out),
                                   &actualBytes);
        if (status == ERROR_MORE_DATA) {
            *characters = actualBytes / sizeof(TCHAR) + 1;
            return status;
        }
        if (status != ERROR_SUCCESS)
            return status;
        if ((type != REG_SZ && type != REG_EXPAND_SZ) ||
            actualBytes % sizeof(TCHAR) != 0)
            return ERROR_INVALID_DATA;
        ULONG actualChars = actualBytes / sizeof(TCHAR);
        if (actualChars == 0) {
            out[0] = 0;
            *characters = 1;
            return ERROR_SUCCESS;
        }
        if (out[actualChars - 1] != 0) {
            if (actualChars == capacity) {
                *characters = actualChars + 1;
                return ERROR_MORE_DATA;
            }
            out[actualChars++] = 0;
        }
        *characters = actualChars;
        return ERROR_SUCCESS;
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
