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

// ATL's opaque POSITION is a stable pointer-sized list cursor. The
// pointed-to type is intentionally incomplete: applications must not
// dereference it or retain a POSITION after its node has been removed.
struct __POSITION;
typedef __POSITION* POSITION;

namespace ATL {

template <typename E>
struct CElementTraits {
    using INARGTYPE = const E&;
    using OUTARGTYPE = E&;
    static bool CompareElements(const E& a, const E& b) { return a == b; }
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


// ATL-style doubly linked list with stable opaque POSITION handles.
// The RATL API names are followed, but not its block allocator or node
// manipulation implementation. Each node is owned and destroyed exactly
// once; nontrivial E objects are never memcpy-relocated.
//
// POSITION values are valid only while their node belongs to this list.
// This implementation validates foreign/removed positions by identity
// before dereferencing them. Validation is O(n), favoring safety over
// ATL's unchecked constant-time handle operations.
template <typename E, class ETraits = CElementTraits<E>>
class CAtlList {
public:
    using INARGTYPE = typename ETraits::INARGTYPE;

    CAtlList() noexcept = default;
    ~CAtlList() noexcept { RemoveAll(); }
    CAtlList(const CAtlList&) = delete;
    CAtlList& operator=(const CAtlList&) = delete;

    CAtlList(CAtlList&& other) noexcept
        : head_(std::exchange(other.head_, nullptr)),
          tail_(std::exchange(other.tail_, nullptr)),
          count_(std::exchange(other.count_, 0)) {}

    CAtlList& operator=(CAtlList&& other) noexcept {
        if (this != &other) {
            RemoveAll();
            head_ = std::exchange(other.head_, nullptr);
            tail_ = std::exchange(other.tail_, nullptr);
            count_ = std::exchange(other.count_, 0);
        }
        return *this;
    }

    size_t GetCount() const noexcept { return count_; }
    bool IsEmpty() const noexcept { return count_ == 0; }
    POSITION GetHeadPosition() const noexcept { return Position(head_); }
    POSITION GetTailPosition() const noexcept { return Position(tail_); }

    E& GetHead() { return MustOwn(GetHeadPosition())->value; }
    const E& GetHead() const { return MustOwn(GetHeadPosition())->value; }
    E& GetTail() { return MustOwn(GetTailPosition())->value; }
    const E& GetTail() const { return MustOwn(GetTailPosition())->value; }

    E& GetNext(POSITION& pos) {
        Node* node = MustOwn(pos);
        pos = Position(node->next);
        return node->value;
    }
    const E& GetNext(POSITION& pos) const {
        Node* node = MustOwn(pos);
        pos = Position(node->next);
        return node->value;
    }
    E& GetPrev(POSITION& pos) {
        Node* node = MustOwn(pos);
        pos = Position(node->prev);
        return node->value;
    }
    const E& GetPrev(POSITION& pos) const {
        Node* node = MustOwn(pos);
        pos = Position(node->prev);
        return node->value;
    }
    E& GetAt(POSITION pos) { return MustOwn(pos)->value; }
    const E& GetAt(POSITION pos) const { return MustOwn(pos)->value; }

    void SetAt(POSITION pos, INARGTYPE value) { MustOwn(pos)->value = value; }

    POSITION AddHead(INARGTYPE value) { return Link(nullptr, head_, value); }
    POSITION AddTail(INARGTYPE value) { return Link(tail_, nullptr, value); }

    // Like ATL, a null insertion cursor denotes the corresponding end.
    POSITION InsertBefore(POSITION pos, INARGTYPE value) {
        if (pos == nullptr) return AddHead(value);
        Node* old = MustOwn(pos);
        return Link(old->prev, old, value);
    }
    POSITION InsertAfter(POSITION pos, INARGTYPE value) {
        if (pos == nullptr) return AddTail(value);
        Node* old = MustOwn(pos);
        return Link(old, old->next, value);
    }

    E RemoveHead() {
        Node* node = MustOwn(GetHeadPosition());
        E result(node->value); // A throwing copy leaves the list unchanged.
        Unlink(node);
        return result;
    }
    E RemoveTail() {
        Node* node = MustOwn(GetTailPosition());
        E result(node->value);
        Unlink(node);
        return result;
    }
    void RemoveAt(POSITION pos) { Unlink(MustOwn(pos)); }

    void RemoveAll() noexcept {
        Node* current = head_;
        while (current != nullptr) {
            Node* next = current->next;
            delete current;
            current = next;
        }
        head_ = nullptr;
        tail_ = nullptr;
        count_ = 0;
    }

    POSITION Find(INARGTYPE value, POSITION posStartAfter = nullptr) const {
        Node* node = posStartAfter == nullptr
            ? head_ : MustOwn(posStartAfter)->next;
        for (; node != nullptr; node = node->next)
            if (ETraits::CompareElements(node->value, value))
                return Position(node);
        return nullptr;
    }

    POSITION FindIndex(size_t index) const noexcept {
        if (index >= count_) return nullptr;
        Node* node = head_;
        while (index-- > 0) node = node->next;
        return Position(node);
    }

    // Relink nodes, rather than swapping E values. POSITION remains bound
    // to the same object even for adjacent head/tail swaps.
    void SwapElements(POSITION first, POSITION second) {
        Node* a = MustOwn(first);
        Node* b = MustOwn(second);
        if (a == b) return;
        if (b->next == a) std::swap(a, b);
        if (a->next == b) {
            Node* before = a->prev;
            Node* after = b->next;
            if (before) before->next = b;
            else head_ = b;
            if (after) after->prev = a;
            else tail_ = a;
            b->prev = before;
            b->next = a;
            a->prev = b;
            a->next = after;
            return;
        }
        Node* ap = a->prev;
        Node* an = a->next;
        Node* bp = b->prev;
        Node* bn = b->next;
        if (ap) ap->next = b; else head_ = b;
        if (an) an->prev = b; else tail_ = b;
        if (bp) bp->next = a; else head_ = a;
        if (bn) bn->prev = a; else tail_ = a;
        a->prev = bp;
        a->next = bn;
        b->prev = ap;
        b->next = an;
    }

private:
    struct Node {
        Node* prev;
        Node* next;
        E value;
        explicit Node(INARGTYPE element)
            : prev(nullptr), next(nullptr), value(element) {}
    };

    static POSITION Position(Node* node) noexcept {
        return reinterpret_cast<POSITION>(node);
    }

    Node* MustOwn(POSITION cursor) const {
        for (Node* node = head_; node != nullptr; node = node->next) {
            if (Position(node) == cursor) return node;
        }
        throw std::out_of_range("CAtlList: stale or foreign POSITION");
    }

    POSITION Link(Node* before, Node* after, INARGTYPE value) {
        if (count_ == (std::numeric_limits<size_t>::max)())
            throw std::length_error("CAtlList: size limit reached");
        // Allocate and copy before modifying any existing links. A failed
        // allocation/copy therefore preserves head, tail and count.
        Node* node = new Node(value);
        node->prev = before;
        node->next = after;
        if (before) before->next = node;
        else head_ = node;
        if (after) after->prev = node;
        else tail_ = node;
        ++count_;
        return Position(node);
    }

    void Unlink(Node* node) noexcept {
        if (node->prev) node->prev->next = node->next;
        else head_ = node->next;
        if (node->next) node->next->prev = node->prev;
        else tail_ = node->prev;
        delete node;
        --count_;
    }

    Node* head_ = nullptr;
    Node* tail_ = nullptr;
    size_t count_ = 0;
};

} // namespace ATL
#endif // __ATLCOLL_H__
