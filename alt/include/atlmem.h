// OpenATL - Win32 heap memory manager utilities (C++17)
// Public API shape informed by ATL and RATL; implementation belongs to OpenATL.
// No COM apartment, registry, COM server, or non-Windows runtime required.
#ifndef __ATLMEM_H__
#define __ATLMEM_H__
#pragma once

#include "atldef.h"
#include <windows.h>
#include <cstddef>

namespace ATL {

class IAtlMemMgr {
public:
    virtual ~IAtlMemMgr() = default;
    virtual void* Allocate(size_t bytes) noexcept = 0;
    virtual void Free(void* buffer) noexcept = 0;
    virtual void* Reallocate(void* buffer, size_t bytes) noexcept = 0;
    virtual size_t GetSize(void* buffer) noexcept = 0;
};

// Non-owning wrapper: neither destroys nor changes the underlying heap.
// Default is GetProcessHeap(), which is valid without COM initialization.
class CWin32Heap final : public IAtlMemMgr {
public:
    HANDLE m_hHeap;

    CWin32Heap() noexcept : m_hHeap(::GetProcessHeap()) {}
    explicit CWin32Heap(HANDLE heap) noexcept : m_hHeap(heap) {}

    void* Allocate(size_t bytes) noexcept override {
        if (m_hHeap == nullptr || bytes == 0)
            return nullptr;
        return ::HeapAlloc(m_hHeap, 0, bytes);
    }

    void Free(void* buffer) noexcept override {
        if (buffer != nullptr && m_hHeap != nullptr)
            (void)::HeapFree(m_hHeap, 0, buffer);
    }

    void* Reallocate(void* buffer, size_t bytes) noexcept override {
        if (buffer == nullptr)
            return Allocate(bytes);
        if (bytes == 0) {
            Free(buffer);
            return nullptr;
        }
        if (m_hHeap == nullptr)
            return nullptr;
        // On failure HeapReAlloc preserves the original block.
        return ::HeapReAlloc(m_hHeap, 0, buffer, bytes);
    }

    size_t GetSize(void* buffer) noexcept override {
        if (m_hHeap == nullptr || buffer == nullptr)
            return static_cast<size_t>(-1);
        return ::HeapSize(m_hHeap, 0, buffer);
    }
};

} // namespace ATL
#endif // __ATLMEM_H__
