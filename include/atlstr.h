// OpenATL - Clean-room ATL subset for WTL 10.0
// String compatibility stub

#ifndef __ATLSTR_H__
#define __ATLSTR_H__

#pragma once

// Define the guard so WTL's optional CString code activates
// WTL checks for __ATLSTR_H__ to enable CString-based overloads

#include "atlbase.h"
#include <string>
#include <algorithm>
#include <vector>
#include <cstdarg>

namespace ATL {

// Minimal CString implementation for WTL compatibility
class CString {
public:
    CString() noexcept {}

    CString(const TCHAR* psz)
    {
        if (psz != NULL)
            m_str = psz;
    }

    CString(const TCHAR* psz, int nLength)
    {
        if (psz != NULL && nLength > 0)
            m_str.assign(psz, nLength);
    }

#ifdef UNICODE
    // Construct from narrow string (ANSI -> UNICODE conversion)
    CString(const char* psz)
    {
        if (psz != NULL) {
            int len = ::MultiByteToWideChar(CP_ACP, 0, psz, -1, NULL, 0);
            if (len > 0) {
                // Conversion includes a final NUL: allocate *len* chars.
                std::vector<wchar_t> output(static_cast<size_t>(len));
                if (::MultiByteToWideChar(CP_ACP, 0, psz, -1,
                      output.data(), len) == len)
                    m_str.assign(output.data(), static_cast<size_t>(len - 1));
            }
        }
    }
#endif

    CString(const CString& strSrc) : m_str(strSrc.m_str) {}
    CString(CString&& strSrc) noexcept : m_str(std::move(strSrc.m_str)) {}

    CString(TCHAR ch, int nRepeat = 1)
    {
        m_str.assign(nRepeat, ch);
    }

    ~CString() {}

    CString& operator=(const CString& strSrc)
    {
        m_str = strSrc.m_str;
        return *this;
    }

    CString& operator=(CString&& strSrc) noexcept
    {
        m_str = std::move(strSrc.m_str);
        return *this;
    }

    CString& operator=(const TCHAR* psz)
    {
        if (psz != NULL)
            m_str = psz;
        else
            m_str.clear();
        return *this;
    }

    CString& operator=(TCHAR ch)
    {
        m_str = ch;
        return *this;
    }

    operator LPCTSTR() const noexcept { return m_str.c_str(); }

    int GetLength() const noexcept { return (int)m_str.length(); }
    bool IsEmpty() const noexcept { return m_str.empty(); }
    void Empty() { m_str.clear(); }

    TCHAR GetAt(int nIndex) const { return m_str.at(nIndex); }
    void SetAt(int nIndex, TCHAR ch) { m_str[nIndex] = ch; }
    TCHAR operator[](int nIndex) const { return m_str[nIndex]; }

    CString& operator+=(const CString& strSrc) { m_str += strSrc.m_str; return *this; }
    CString& operator+=(const TCHAR* psz) { if (psz) m_str += psz; return *this; }
    CString& operator+=(TCHAR ch) { m_str += ch; return *this; }

    friend CString operator+(const CString& a, const CString& b)
    {
        CString s;
        s.m_str = a.m_str + b.m_str;
        return s;
    }

    friend CString operator+(const CString& a, const TCHAR* b)
    {
        CString s;
        s.m_str = a.m_str;
        if (b) s.m_str += b;
        return s;
    }

    friend CString operator+(const TCHAR* a, const CString& b)
    {
        CString s;
        if (a) s.m_str = a;
        s.m_str += b.m_str;
        return s;
    }

    int Compare(const TCHAR* psz) const noexcept
    {
        return _tcscmp(m_str.c_str(), psz ? psz : _T(""));
    }

    int CompareNoCase(const TCHAR* psz) const noexcept
    {
        return _tcsicmp(m_str.c_str(), psz ? psz : _T(""));
    }

    bool operator==(const TCHAR* psz) const { return Compare(psz) == 0; }
    bool operator!=(const TCHAR* psz) const { return Compare(psz) != 0; }
    bool operator<(const TCHAR* psz) const { return Compare(psz) < 0; }
    bool operator>(const TCHAR* psz) const { return Compare(psz) > 0; }

    CString Mid(int nFirst) const { return CString(m_str.substr(nFirst).c_str()); }
    CString Mid(int nFirst, int nCount) const { return CString(m_str.substr(nFirst, nCount).c_str()); }
    CString Left(int nCount) const { return CString(m_str.substr(0, nCount).c_str()); }
    CString Right(int nCount) const { return CString(m_str.substr(m_str.length() - nCount).c_str()); }

    int Find(TCHAR ch, int nStart = 0) const
    {
        auto pos = m_str.find(ch, nStart);
        return pos == std::basic_string<TCHAR>::npos ? -1 : (int)pos;
    }

    int Find(const TCHAR* psz, int nStart = 0) const
    {
        if (psz == NULL) return -1;
        auto pos = m_str.find(psz, nStart);
        return pos == std::basic_string<TCHAR>::npos ? -1 : (int)pos;
    }

    int ReverseFind(TCHAR ch) const
    {
        auto pos = m_str.rfind(ch);
        return pos == std::basic_string<TCHAR>::npos ? -1 : (int)pos;
    }

    CString& MakeUpper() { for (auto& c : m_str) c = (TCHAR)_totupper(c); return *this; }
    CString& MakeLower() { for (auto& c : m_str) c = (TCHAR)_totlower(c); return *this; }

    CString& TrimLeft()
    {
        auto it = m_str.begin();
        while (it != m_str.end() && _istspace(*it)) ++it;
        m_str.erase(m_str.begin(), it);
        return *this;
    }

    CString& TrimRight()
    {
        auto it = m_str.end();
        while (it != m_str.begin() && _istspace(*(it - 1))) --it;
        m_str.erase(it, m_str.end());
        return *this;
    }

    CString& Trim() { TrimLeft(); TrimRight(); return *this; }

    LPTSTR GetBuffer(int nMinBufLength = 0)
    {
        if (nMinBufLength > GetLength())
            m_str.resize(static_cast<size_t>(nMinBufLength));
        return m_str.data(); // C++17: valid even for an empty CString
    }

    void ReleaseBuffer(int nNewLength = -1)
    {
        if (nNewLength >= 0)
            m_str.resize(nNewLength);
        else
            m_str.resize(_tcslen(m_str.c_str()));
    }

    LPTSTR GetBufferSetLength(int nNewLength)
    {
        m_str.resize(static_cast<size_t>(nNewLength < 0 ? 0 : nNewLength));
        return m_str.data();
    }

    void __cdecl Format(const TCHAR* pszFormat, ...)
    {
        va_list args;
        va_start(args, pszFormat);
        FormatV(pszFormat, args);
        va_end(args);
    }

    void FormatV(const TCHAR* pszFormat, va_list args)
    {
        if (!pszFormat) { m_str.clear(); return; }
#if defined(_MSC_VER) && !defined(__clang__)
        va_list measurement = args;
#else
        va_list measurement;
        va_copy(measurement, args);
#endif
        const int length = _vsctprintf(pszFormat, measurement);
#if !defined(_MSC_VER) || defined(__clang__)
        va_end(measurement);
#endif
        if (length < 0) { m_str.clear(); return; }
        std::vector<TCHAR> buffer(static_cast<size_t>(length) + 1);
        const int written = _vstprintf_s(buffer.data(), buffer.size(),
                                        pszFormat, args);
        if (written < 0) { m_str.clear(); return; }
        m_str.assign(buffer.data(), static_cast<size_t>(written));
    }

    BOOL LoadString(UINT nID)
    {
        TCHAR szBuf[256];
        int nLen = ::LoadString(_AtlBaseModule.GetResourceInstance(), nID, szBuf, 256);
        if (nLen > 0) {
            m_str.assign(szBuf, nLen);
            return TRUE;
        }
        return FALSE;
    }

    int Replace(const TCHAR* pszOld, const TCHAR* pszNew)
    {
        if (pszOld == NULL || pszNew == NULL) return 0;
        int nCount = 0;
        size_t nOldLen = _tcslen(pszOld);
        if (nOldLen == 0) return 0; // avoid endless empty-pattern replace
        size_t nNewLen = _tcslen(pszNew);
        size_t pos = 0;
        while ((pos = m_str.find(pszOld, pos)) != std::basic_string<TCHAR>::npos) {
            m_str.replace(pos, nOldLen, pszNew);
            pos += nNewLen;
            nCount++;
        }
        return nCount;
    }

    int Replace(TCHAR chOld, TCHAR chNew)
    {
        int nCount = 0;
        for (auto& c : m_str) {
            if (c == chOld) { c = chNew; nCount++; }
        }
        return nCount;
    }

    int Delete(int nIndex, int nCount = 1)
    {
        m_str.erase(nIndex, nCount);
        return (int)m_str.length();
    }

    int Insert(int nIndex, TCHAR ch)
    {
        m_str.insert(m_str.begin() + nIndex, ch);
        return (int)m_str.length();
    }

    int Insert(int nIndex, const TCHAR* psz)
    {
        if (psz) m_str.insert(nIndex, psz);
        return (int)m_str.length();
    }

private:
    std::basic_string<TCHAR> m_str;
};

} // namespace ATL

#endif // __ATLSTR_H__
