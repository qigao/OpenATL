// OpenATL - typed ATL-style array utilities (C++17).
// API compatibility inspired by ATL / RATL; independent safe implementation.
#ifndef __ATLCOLL_H__
#define __ATLCOLL_H__
#pragma once

#include "atldef.h"
#include <cstddef>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ATL {

template <typename E>
struct CElementTraits {
    using INARGTYPE = const E&;
    using OUTARGTYPE = E&;
};

// A bounded-owner container with ATL's familiar Add, SetCount, and GetAt.
// Backed by std::vector to avoid relocating live nontrivial objects with
// memcpy; pointers returned by GetData are invalidated by growth/removal.
template <typename E, class ETraits = CElementTraits<E>>
class CAtlArray {
public:
    using INARGTYPE = typename ETraits::INARGTYPE;
    using OUTARGTYPE = typename ETraits::OUTARGTYPE;

    CAtlArray() = default;
    ~CAtlArray() = default;
    CAtlArray(const CAtlArray&) = delete;
    CAtlArray& operator=(const CAtlArray&) = delete;
    CAtlArray(CAtlArray&&) = default;
    CAtlArray& operator=(CAtlArray&&) = default;

    size_t Add(INARGTYPE value) {
        items_.push_back(value);
        return items_.size() - 1;
    }
    size_t Add() {
        items_.emplace_back();
        return items_.size() - 1;
    }

    // Returns false on allocation/length failure, preserving existing items.
    // nGrowBy is an optional capacity hint, not an element count.
    bool SetCount(size_t count, int nGrowBy = -1) {
        try {
            if (nGrowBy >= 0)
                grow_by_ = static_cast<size_t>(nGrowBy);
            if (count == 0) {
                std::vector<E> empty;
                items_.swap(empty);
                return true;
            }
            if (count > items_.capacity() && grow_by_ != 0) {
                const size_t max = items_.max_size();
                if (count > max)
                    return false;
                const size_t remaining = max - count;
                items_.reserve(count + (grow_by_ < remaining ? grow_by_ : remaining));
            }
            items_.resize(count);
            return true;
        } catch (const std::bad_alloc&) {
            return false;
        } catch (const std::length_error&) {
            return false;
        }
    }

    size_t GetCount() const noexcept { return items_.size(); }
    bool IsEmpty() const noexcept { return items_.empty(); }

    E* GetData() noexcept { return items_.empty() ? nullptr : items_.data(); }
    const E* GetData() const noexcept { return items_.empty() ? nullptr : items_.data(); }

    E& GetAt(size_t index) {
        ATLASSERT(index < items_.size());
        return items_[index];
    }
    const E& GetAt(size_t index) const {
        ATLASSERT(index < items_.size());
        return items_[index];
    }
    E& operator[](size_t index) { return GetAt(index); }
    const E& operator[](size_t index) const { return GetAt(index); }

    void SetAt(size_t index, INARGTYPE value) {
        ATLASSERT(index < items_.size());
        items_.at(index) = value;
    }

    void RemoveAt(size_t index, size_t count = 1) {
        if (index > items_.size() || count > items_.size() - index)
            throw std::out_of_range("CAtlArray::RemoveAt");
        items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(index),
                     items_.begin() + static_cast<std::ptrdiff_t>(index + count));
    }

    void RemoveAll() noexcept { items_.clear(); }

private:
    std::vector<E> items_;
    size_t grow_by_ = 0;
};

} // namespace ATL
#endif // __ATLCOLL_H__
