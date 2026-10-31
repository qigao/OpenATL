// OpenATL - native Win32 synchronization wrappers; independent Apache-2.0.
// Mirrors a small public ATL/RATL-style client API, NOT its implementation.
// These are handle owners and never create COM objects or COM servers.
#ifndef OPENATL_ATLSYNC_H
#define OPENATL_ATLSYNC_H
#pragma once

#include "atlbase.h"

namespace ATL {

class CEvent : public CHandle {
public:
    CEvent() noexcept = default;
    explicit CEvent(HANDLE h) noexcept : CHandle(h) {}

    BOOL Create(LPSECURITY_ATTRIBUTES security = NULL,
                BOOL manualReset = FALSE, BOOL initiallySignaled = FALSE,
                LPCTSTR name = NULL) noexcept {
        if (m_h) { ::SetLastError(ERROR_ALREADY_EXISTS); return FALSE; }
        HANDLE h = ::CreateEvent(security, manualReset, initiallySignaled, name);
        if (!h) return FALSE;
        Attach(h);
        return TRUE;
    }
    BOOL Set() noexcept {
        if (!m_h) { ::SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
        return ::SetEvent(m_h);
    }
    BOOL Reset() noexcept {
        if (!m_h) { ::SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
        return ::ResetEvent(m_h);
    }
    DWORD Wait(DWORD milliseconds = INFINITE) noexcept {
        if (!m_h) { ::SetLastError(ERROR_INVALID_HANDLE); return WAIT_FAILED; }
        return ::WaitForSingleObject(m_h, milliseconds);
    }
};

class CMutex : public CHandle {
public:
    CMutex() noexcept = default;
    explicit CMutex(HANDLE h) noexcept : CHandle(h) {}

    BOOL Create(LPSECURITY_ATTRIBUTES security = NULL,
                BOOL initiallyOwned = FALSE, LPCTSTR name = NULL) noexcept {
        if (m_h) { ::SetLastError(ERROR_ALREADY_EXISTS); return FALSE; }
        HANDLE h = ::CreateMutex(security, initiallyOwned, name);
        if (!h) return FALSE;
        Attach(h);
        return TRUE;
    }
    DWORD Wait(DWORD milliseconds = INFINITE) noexcept {
        if (!m_h) { ::SetLastError(ERROR_INVALID_HANDLE); return WAIT_FAILED; }
        return ::WaitForSingleObject(m_h, milliseconds);
    }
    BOOL Release() noexcept {
        if (!m_h) { ::SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
        return ::ReleaseMutex(m_h);
    }
};

class CSemaphore : public CHandle {
public:
    CSemaphore() noexcept = default;
    explicit CSemaphore(HANDLE h) noexcept : CHandle(h) {}

    BOOL Create(LPSECURITY_ATTRIBUTES security = NULL,
                LONG initial = 0, LONG maximum = 1, LPCTSTR name = NULL) noexcept {
        if (m_h) { ::SetLastError(ERROR_ALREADY_EXISTS); return FALSE; }
        if (initial < 0 || maximum <= 0 || initial > maximum) {
            ::SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        HANDLE h = ::CreateSemaphore(security, initial, maximum, name);
        if (!h) return FALSE;
        Attach(h);
        return TRUE;
    }
    DWORD Wait(DWORD milliseconds = INFINITE) noexcept {
        if (!m_h) { ::SetLastError(ERROR_INVALID_HANDLE); return WAIT_FAILED; }
        return ::WaitForSingleObject(m_h, milliseconds);
    }
    BOOL Release(LONG count = 1, LONG* previous = NULL) noexcept {
        if (!m_h) { ::SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
        if (count <= 0) { ::SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
        return ::ReleaseSemaphore(m_h, count, previous);
    }
};

class CCriticalSection {
    CRITICAL_SECTION section_{};
    bool initialized_ = false;
public:
    CCriticalSection() noexcept = default;
    CCriticalSection(const CCriticalSection&) = delete;
    CCriticalSection& operator=(const CCriticalSection&) = delete;
    ~CCriticalSection() {
        // Caller must have quiesced all threads before owner destruction.
        if (initialized_) ::DeleteCriticalSection(&section_);
    }

    HRESULT Init(DWORD spinCount = 0) noexcept {
        if (initialized_) return S_FALSE;
        if (!::InitializeCriticalSectionEx(&section_, spinCount, 0))
            return HRESULT_FROM_WIN32(::GetLastError());
        initialized_ = true;
        return S_OK;
    }
    BOOL Lock() noexcept {
        if (!initialized_) { ::SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
        ::EnterCriticalSection(&section_);
        return TRUE;
    }
    BOOL TryLock() noexcept {
        if (!initialized_) { ::SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
        return ::TryEnterCriticalSection(&section_);
    }
    void Unlock() noexcept {
        if (initialized_) ::LeaveCriticalSection(&section_);
    }
    bool IsInitialized() const noexcept { return initialized_; }
};

// Only lock a correctly initialized critical section. Scope never calls
// Unlock unless Lock actually succeeded; never grants lock ownership across
// threads or uses any COM apartment.
class CScopedCriticalSectionLock {
    CCriticalSection* owner_;
    bool held_;
public:
    explicit CScopedCriticalSectionLock(CCriticalSection& cs) noexcept
      : owner_(&cs), held_(cs.Lock() != FALSE) {}
    CScopedCriticalSectionLock(const CScopedCriticalSectionLock&) = delete;
    CScopedCriticalSectionLock& operator=(const CScopedCriticalSectionLock&) = delete;
    ~CScopedCriticalSectionLock() {
        if (held_) owner_->Unlock();
    }
    bool IsLocked() const noexcept { return held_; }
};

} // namespace ATL
#endif // OPENATL_ATLSYNC_H
