// OpenATL - source-owned ATL-style simple collection containers.
// No raw relocation of constructed C++ objects or shallow owning copies.
#ifndef __ATLSIMPCOLL_H__
#define __ATLSIMPCOLL_H__
#pragma once

#include "atldef.h"
#include <climits>
#include <cstddef>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ATL {

template <typename T>
class CSimpleArrayEqualHelper {
public:
    static bool IsEqual(const T& a, const T& b) { return a == b; }
};

template <typename T>
class CSimpleArrayEqualHelperFalse {
public:
    static bool IsEqual(const T&, const T&) {
        ATLASSERT(FALSE);
        return false;
    }
};

// m_aT, m_nSize and m_nAllocSize retain the usual ATL inspection surface;
// these are non-owning mirrors, NOT an alternate independently owned buffer.
// Mutation is through methods; callers may not modify the count/pointer.
template <typename T, typename TEqual = CSimpleArrayEqualHelper<T>>
class CSimpleArray {
public:
    T* m_aT = nullptr;
    int m_nSize = 0;
    int m_nAllocSize = 0;

    CSimpleArray() noexcept = default;
    CSimpleArray(const CSimpleArray& src) : values_(src.values_) { Sync(); }
    CSimpleArray(CSimpleArray&& src) noexcept
        : values_(std::move(src.values_)) {
        Sync();
        src.Sync();
    }
    CSimpleArray& operator=(const CSimpleArray& src) {
        if (this != &src) {
            CSimpleArray replacement(src);
            values_.swap(replacement.values_);
            Sync();
        }
        return *this;
    }
    CSimpleArray& operator=(CSimpleArray&& src) noexcept {
        if (this != &src) {
            values_ = std::move(src.values_);
            Sync();
            src.Sync();
        }
        return *this;
    }
    ~CSimpleArray() = default;

    int GetSize() const noexcept { return m_nSize; }

    BOOL Add(const T& value) {
        if (m_nSize == INT_MAX) return FALSE;
        try {
            // std::vector handles Add(GetData()[i]) aliasing even on growth:
            // never reallocate live objects with realloc or memmove.
            values_.push_back(value);
        } catch (const std::bad_alloc&) {
            Sync();
            return FALSE;
        } catch (const std::length_error&) {
            Sync();
            return FALSE;
        } catch (...) {
            Sync();
            throw;
        }
        Sync();
        return TRUE;
    }

    BOOL Remove(const T& value) {
        const int index = Find(value);
        return index < 0 ? FALSE : RemoveAt(index);
    }

    BOOL RemoveAt(int index) {
        ATLASSERT(index >= 0 && index < m_nSize);
        if (index < 0 || index >= m_nSize) return FALSE;
        try {
            values_.erase(values_.begin() + index);
        } catch (...) {
            Sync();
            throw;
        }
        Sync();
        return TRUE;
    }

    void RemoveAll() noexcept {
        std::vector<T> empty;
        values_.swap(empty);
        Sync();
    }

    const T& operator[](int index) const {
        ATLASSERT(index >= 0 && index < m_nSize);
        return values_.at(static_cast<size_t>(index));
    }
    T& operator[](int index) {
        ATLASSERT(index >= 0 && index < m_nSize);
        return values_.at(static_cast<size_t>(index));
    }
    T* GetData() const noexcept { return m_aT; }

    int Find(const T& value) const {
        for (int i = 0; i < m_nSize; ++i)
            if (TEqual::IsEqual(values_[static_cast<size_t>(i)], value))
                return i;
        return -1;
    }
    BOOL SetAtIndex(int index, const T& value) {
        ATLASSERT(index >= 0 && index < m_nSize);
        if (index < 0 || index >= m_nSize) return FALSE;
        // Copy before assignment to avoid a self-alias that is invalidated
        // if a value object internally changes its own storage.
        T staged(value);
        values_[static_cast<size_t>(index)] = std::move(staged);
        return TRUE;
    }

private:
    void Sync() noexcept {
        m_aT = values_.empty() ? nullptr : values_.data();
        m_nSize = static_cast<int>(values_.size());
        m_nAllocSize = values_.capacity() > static_cast<size_t>(INT_MAX)
            ? INT_MAX : static_cast<int>(values_.capacity());
    }
    std::vector<T> values_;
};

// Distinct owned key/value vectors preserve the ATL key/value pointer
// accessors. Copy, insert, remove and replacement have explicit object
// lifetimes and cannot leave one side of the map half committed.
template <typename TKey, typename TVal,
          typename TEqual = CSimpleArrayEqualHelper<TKey>>
class CSimpleMap {
public:
    TKey* m_aKey = nullptr;
    TVal* m_aVal = nullptr;
    int m_nSize = 0;

    CSimpleMap() noexcept = default;
    CSimpleMap(const CSimpleMap& src)
        : keys_(src.keys_), values_(src.values_) { Sync(); }
    CSimpleMap(CSimpleMap&& src) noexcept
        : keys_(std::move(src.keys_)), values_(std::move(src.values_)) {
        Sync();
        src.Sync();
    }
    CSimpleMap& operator=(const CSimpleMap& src) {
        if (this != &src) {
            CSimpleMap replacement(src);
            keys_.swap(replacement.keys_);
            values_.swap(replacement.values_);
            Sync();
        }
        return *this;
    }
    CSimpleMap& operator=(CSimpleMap&& src) noexcept {
        if (this != &src) {
            keys_ = std::move(src.keys_);
            values_ = std::move(src.values_);
            Sync();
            src.Sync();
        }
        return *this;
    }
    ~CSimpleMap() = default;

    int GetSize() const noexcept { return m_nSize; }

    BOOL Add(const TKey& key, const TVal& val) {
        if (m_nSize == INT_MAX) return FALSE;
        try {
            // Stage both inputs before growth: arguments may alias keys_/values_.
            TKey nextKey(key);
            TVal nextValue(val);
            keys_.push_back(nextKey);
            try {
                values_.push_back(nextValue);
            } catch (...) {
                keys_.pop_back();
                throw;
            }
        } catch (const std::bad_alloc&) {
            Sync();
            return FALSE;
        } catch (const std::length_error&) {
            Sync();
            return FALSE;
        } catch (...) {
            Sync();
            throw;
        }
        Sync();
        return TRUE;
    }

    BOOL Remove(const TKey& key) {
        const int index = FindKey(key);
        return index < 0 ? FALSE : RemoveAt(index);
    }
    BOOL RemoveAt(int index) {
        ATLASSERT(index >= 0 && index < m_nSize);
        if (index < 0 || index >= m_nSize) return FALSE;
        // Both erase operations are prepared before either is published;
        // if copying/moving user objects throws, the map is unchanged.
        std::vector<TKey> stagedKeys(keys_);
        std::vector<TVal> stagedValues(values_);
        stagedKeys.erase(stagedKeys.begin() + index);
        stagedValues.erase(stagedValues.begin() + index);
        keys_.swap(stagedKeys);
        values_.swap(stagedValues);
        Sync();
        return TRUE;
    }
    void RemoveAll() noexcept {
        std::vector<TKey> emptyKeys;
        std::vector<TVal> emptyValues;
        keys_.swap(emptyKeys);
        values_.swap(emptyValues);
        Sync();
    }
    BOOL SetAt(const TKey& key, const TVal& val) {
        const int index = FindKey(key);
        if (index < 0) return FALSE;
        // Keep the old map valid if user assignment throws.
        std::vector<TVal> replacement(values_);
        replacement[static_cast<size_t>(index)] = val;
        values_.swap(replacement);
        Sync();
        return TRUE;
    }
    TVal Lookup(const TKey& key) const {
        int index = FindKey(key);
        return index < 0 ? TVal() : values_[static_cast<size_t>(index)];
    }
    TKey ReverseLookup(const TVal& val) const {
        int index = FindVal(val);
        return index < 0 ? TKey() : keys_[static_cast<size_t>(index)];
    }
    TKey& GetKeyAt(int index) const {
        ATLASSERT(index >= 0 && index < m_nSize);
        return const_cast<TKey&>(keys_.at(static_cast<size_t>(index)));
    }
    TVal& GetValueAt(int index) const {
        ATLASSERT(index >= 0 && index < m_nSize);
        return const_cast<TVal&>(values_.at(static_cast<size_t>(index)));
    }
    int FindKey(const TKey& key) const {
        for (int i = 0; i < m_nSize; ++i)
            if (TEqual::IsEqual(keys_[static_cast<size_t>(i)], key))
                return i;
        return -1;
    }
    int FindVal(const TVal& val) const {
        for (int i = 0; i < m_nSize; ++i)
            if (CSimpleArrayEqualHelper<TVal>::IsEqual(
                    values_[static_cast<size_t>(i)], val))
                return i;
        return -1;
    }
    BOOL SetAtIndex(int index, const TKey& key, const TVal& val) {
        ATLASSERT(index >= 0 && index < m_nSize);
        if (index < 0 || index >= m_nSize) return FALSE;
        std::vector<TKey> stagedKeys(keys_);
        std::vector<TVal> stagedValues(values_);
        stagedKeys[static_cast<size_t>(index)] = key;
        stagedValues[static_cast<size_t>(index)] = val;
        keys_.swap(stagedKeys);
        values_.swap(stagedValues);
        Sync();
        return TRUE;
    }

private:
    void Sync() noexcept {
        ATLASSERT(keys_.size() == values_.size());
        m_aKey = keys_.empty() ? nullptr : keys_.data();
        m_aVal = values_.empty() ? nullptr : values_.data();
        m_nSize = static_cast<int>(keys_.size());
    }
    std::vector<TKey> keys_;
    std::vector<TVal> values_;
};

template <typename T>
class CSimpleValArray : public CSimpleArray<T, CSimpleArrayEqualHelperFalse<T>> {
public:
    BOOL Add(T value) {
        return CSimpleArray<T, CSimpleArrayEqualHelperFalse<T>>::Add(value);
    }
    BOOL Remove(T value) {
        return CSimpleArray<T, CSimpleArrayEqualHelperFalse<T>>::Remove(value);
    }
    T operator[](int index) const {
        return CSimpleArray<T, CSimpleArrayEqualHelperFalse<T>>::operator[](index);
    }
};

} // namespace ATL
#endif // __ATLSIMPCOLL_H__
