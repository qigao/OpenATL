// Native API conformance for additional ATL-style utilities.
// No COM apartment or COM server. MSVC and llvm-mingw x64/ARM64 coverage.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <ole2.h>
#include <atlmem.h>
#include <atltime.h>
#include <atlcoll.h>
#include <atldef.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

#if !defined(OPENATL_REPOSITORY_HEADERS) || OPENATL_REPOSITORY_HEADERS != 0x4F41544CUL
#error "The ATL extension conformance must compile in-tree OpenATL headers"
#endif

static int fail(int code, const char* phase) {
    std::fprintf(stderr, "ATL_EXTENSIONS_FAIL code=%d phase=%s\n", code, phase);
    return code;
}

struct Tracked {
    static int live;
    int value;
    Tracked() noexcept : value(0) { ++live; }
    explicit Tracked(int v) noexcept : value(v) { ++live; }
    Tracked(const Tracked& other) noexcept : value(other.value) { ++live; }
    Tracked(Tracked&& other) noexcept : value(other.value) { ++live; }
    Tracked& operator=(const Tracked& other) noexcept {
        value = other.value; return *this;
    }
    Tracked& operator=(Tracked&& other) noexcept {
        value = other.value; return *this;
    }
    ~Tracked() { --live; }
};
int Tracked::live = 0;

static int test_heap() {
    ATL::CWin32Heap heap;
    ATL::IAtlMemMgr* manager = &heap;
    void* p = manager->Allocate(32);
    if (!p) return fail(11, "heap_allocate");
    if (manager->GetSize(p) < 32) {
        manager->Free(p);
        return fail(12, "heap_size");
    }
    std::memset(p, 0x5A, 32);
    void* grown = manager->Reallocate(p, 128);
    if (!grown) {
        manager->Free(p);
        return fail(13, "heap_realloc");
    }
    const unsigned char* bytes = static_cast<const unsigned char*>(grown);
    for (int i = 0; i < 32; ++i) {
        if (bytes[i] != 0x5A) {
            manager->Free(grown);
            return fail(14, "heap_preserve");
        }
    }
    manager->Free(grown);
    manager->Free(nullptr);
    void* scratch = manager->Reallocate(nullptr, 12);
    if (!scratch || manager->Reallocate(scratch, 0) != nullptr)
        return fail(15, "heap_zero_realloc");
    ATL::CWin32Heap invalid(nullptr);
    if (invalid.Allocate(1) != nullptr ||
        invalid.GetSize(nullptr) != static_cast<size_t>(-1))
        return fail(16, "heap_null");
    return 0;
}

static int test_filetime() {
    static_assert(sizeof(ATL::CFileTime) == sizeof(FILETIME), "FILETIME ABI must match");
    static_assert(ATL::CFileTime::Second == 10000000ULL, "Win32 FILETIME tick rate");
    ATL::CFileTime start(100ULL);
    ATL::CFileTimeSpan delay(20);
    const ATL::CFileTime end = start + delay;
    if (end.GetTime() != 120ULL ||
        (end - start).GetTimeSpan() != 20 ||
        !(end > start) || !(start < end))
        return fail(21, "filetime_arithmetic");
    ATL::CFileTime unchanged(999ULL);
    if (start.TryAdd(ATL::CFileTimeSpan(-101), &unchanged) ||
        unchanged.GetTime() != 999ULL)
        return fail(22, "filetime_underflow");
    const ATL::CFileTime maximum((std::numeric_limits<ULONGLONG>::max)());
    if (maximum.TryAdd(ATL::CFileTimeSpan(1), &unchanged) ||
        unchanged.GetTime() != 999ULL ||
        start.TryAdd(delay, nullptr))
        return fail(23, "filetime_overflow");
    if (!start.TryAdd(delay, &unchanged) || unchanged != end)
        return fail(24, "filetime_checked_add");
    ATL::CFileTimeSpan out(4);
    if (!delay.TryAdd(ATL::CFileTimeSpan(-15), &out) ||
        out.GetTimeSpan() != 5)
        return fail(25, "span_checked_add");
    if (ATL::CFileTimeSpan((std::numeric_limits<LONGLONG>::max)())
        .TryAdd(ATL::CFileTimeSpan(1), &out) || out.GetTimeSpan() != 5)
        return fail(26, "span_overflow");
    if (ATL::CFileTime::GetCurrentTime().GetTime() == 0)
        return fail(27, "filetime_system_clock");
    return 0;
}

static int test_array() {
    ATL::CAtlArray<int> integers;
    if (!integers.IsEmpty() || integers.GetData() != nullptr ||
        integers.Add(7) != 0 || integers.Add(11) != 1 ||
        integers.GetCount() != 2 || integers.GetAt(1) != 11)
        return fail(31, "array_add");
    integers.SetAt(0, 9);
    if (!integers.SetCount(4, 8) || integers.GetCount() != 4 ||
        integers[0] != 9 || integers[1] != 11)
        return fail(32, "array_grow");
    if (integers.SetCount((std::numeric_limits<size_t>::max)()) ||
        integers.GetCount() != 4 || integers[0] != 9)
        return fail(33, "array_fail_closed");
    integers.RemoveAt(1, 2);
    if (integers.GetCount() != 2 || integers[1] != 0)
        return fail(34, "array_remove");
    integers.RemoveAll();
    if (!integers.IsEmpty() || integers.GetData() != nullptr)
        return fail(35, "array_empty");
    {
        ATL::CAtlArray<Tracked> objects;
        Tracked one(42);
        if (objects.Add(one) != 0 ||
            !objects.SetCount(3) ||
            objects.GetAt(0).value != 42)
            return fail(36, "array_object");
        objects.RemoveAt(1, 1);
        if (objects.GetCount() != 2 ||
            Tracked::live != 3)
            return fail(37, "array_object_lifetime");
    }
    if (Tracked::live != 0)
        return fail(38, "array_destructor");
    return 0;
}


struct ThrowingCopy {
    static bool reject;
    int value;
    explicit ThrowingCopy(int n) noexcept : value(n) {}
    ThrowingCopy(const ThrowingCopy& other) : value(other.value) {
        if (reject) throw std::runtime_error("injected copy failure");
    }
    ThrowingCopy& operator=(const ThrowingCopy&) = default;
};
bool ThrowingCopy::reject = false;

static int test_list() {
    static_assert(!std::is_copy_constructible<ATL::CAtlList<int>>::value,
                  "CAtlList must own its nodes exclusively");
    static_assert(std::is_nothrow_move_constructible<ATL::CAtlList<int>>::value,
                  "Moving ownership of a list cannot allocate");

    ATL::CAtlList<int> list;
    if (!list.IsEmpty() || list.GetCount() != 0 ||
        list.GetHeadPosition() || list.GetTailPosition() ||
        list.FindIndex(0) || list.Find(15))
        return fail(41, "list_empty");

    POSITION a = list.AddTail(10);
    POSITION d = list.AddTail(40);
    POSITION b = list.InsertAfter(a, 20);
    POSITION c = list.InsertBefore(d, 30);
    if (!a || !b || !c || !d || list.GetCount() != 4 ||
        list.GetHead() != 10 || list.GetTail() != 40 ||
        list.GetAt(b) != 20 || list.Find(30) != c ||
        list.FindIndex(0) != a || list.FindIndex(3) != d ||
        list.FindIndex(4) != nullptr || list.Find(10, a) != nullptr)
        return fail(42, "list_insertion");

    const ATL::CAtlList<int>& readOnly = list;
    POSITION forward = readOnly.GetHeadPosition();
    const int expectedForward[] = {10, 20, 30, 40};
    for (int value : expectedForward)
        if (forward == nullptr || readOnly.GetNext(forward) != value)
            return fail(43, "list_forward_iteration");
    if (forward != nullptr)
        return fail(44, "list_forward_termination");
    POSITION backward = list.GetTailPosition();
    const int expectedBackward[] = {40, 30, 20, 10};
    for (int value : expectedBackward)
        if (backward == nullptr || list.GetPrev(backward) != value)
            return fail(45, "list_backward_iteration");
    if (backward != nullptr)
        return fail(46, "list_backward_termination");

    list.SwapElements(b, c); // Adjacent: 10,30,20,40.
    if (list.GetAt(b) != 20 || list.GetAt(c) != 30 ||
        list.FindIndex(1) != c || list.FindIndex(2) != b)
        return fail(47, "list_adjacent_swap_stable_positions");

    list.SwapElements(a, d); // Nonadjacent heads/tails: 40,30,20,10.
    if (list.GetHeadPosition() != d || list.GetTailPosition() != a ||
        list.GetHead() != 40 || list.GetTail() != 10 ||
        list.FindIndex(1) != c || list.FindIndex(2) != b)
        return fail(48, "list_nonadjacent_swap");

    list.SwapElements(b, c); // Reversed adjacent.
    if (list.FindIndex(1) != b || list.FindIndex(2) != c)
        return fail(49, "list_reverse_adjacent_swap");
    list.SwapElements(a, a);
    if (list.GetTailPosition() != a)
        return fail(50, "list_same_position_swap");

    ATL::CAtlList<int> foreign;
    POSITION foreignPos = foreign.AddTail(999);
    try {
        list.RemoveAt(foreignPos);
        return fail(51, "list_foreign_cursor_admitted");
    } catch (const std::out_of_range&) {}
    if (list.GetCount() != 4 || list.GetTailPosition() != a)
        return fail(52, "list_foreign_cursor_state");
    try {
        (void)list.GetAt(nullptr);
        return fail(53, "list_null_cursor_admitted");
    } catch (const std::out_of_range&) {}
    try {
        (void)list.Find(999, foreignPos);
        return fail(54, "list_foreign_find_cursor_admitted");
    } catch (const std::out_of_range&) {}

    // Moving the list keeps live POSITION handles with the objects.
    ATL::CAtlList<int> moved(std::move(list));
    if (!list.IsEmpty() || !moved.GetCount() ||
        moved.GetHeadPosition() != d || moved.GetTailPosition() != a ||
        moved.GetAt(a) != 10)
        return fail(55, "list_move_owner");

    moved.RemoveAt(c);
    if (moved.GetCount() != 3 || moved.Find(30) != nullptr ||
        moved.RemoveHead() != 40 || moved.RemoveTail() != 10 ||
        moved.GetHead() != 20)
        return fail(56, "list_unlink_head_tail");
    moved.RemoveAll();
    if (!moved.IsEmpty() || moved.GetHeadPosition() || moved.GetTailPosition())
        return fail(57, "list_remove_all");

    {
        ATL::CAtlList<Tracked> objects;
        Tracked value(7);
        POSITION first = objects.AddTail(value);
        POSITION second = objects.AddHead(value);
        if (Tracked::live != 3 || objects.GetCount() != 2 ||
            objects.GetAt(first).value != 7 ||
            objects.GetAt(second).value != 7)
            return fail(58, "list_nontrivial_lifetime");
        objects.RemoveAt(first);
        if (Tracked::live != 2 || objects.GetCount() != 1)
            return fail(59, "list_nontrivial_unlink");
        objects.RemoveAll();
        if (Tracked::live != 1 || objects.GetCount() != 0)
            return fail(60, "list_nontrivial_cleanup");
    }
    if (Tracked::live != 0)
        return fail(61, "list_node_leak");

    // A throwing constructor/copy cannot attach half-constructed nodes
    // or change list order/count. RemoveHead must copy before unlinking.
    ATL::CAtlList<ThrowingCopy> exceptionList;
    ThrowingCopy original(9);
    POSITION only = exceptionList.AddTail(original);
    ThrowingCopy::reject = true;
    bool appendThrew = false;
    try { (void)exceptionList.AddTail(original); }
    catch (const std::runtime_error&) { appendThrew = true; }
    bool removeThrew = false;
    try { (void)exceptionList.RemoveHead(); }
    catch (const std::runtime_error&) { removeThrew = true; }
    ThrowingCopy::reject = false;
    if (!appendThrew || !removeThrew || exceptionList.GetCount() != 1 ||
        exceptionList.GetHeadPosition() != only ||
        exceptionList.GetHead().value != 9)
        return fail(62, "list_exception_guarantee");
    exceptionList.RemoveAll();
    return 0;
}

int main() {
    static_assert(sizeof(void*) == 8, "Only Windows x64 and ARM64 qualified");
    ULONG_PTR token = 0;
    if (::CoGetContextToken(&token) != CO_E_NOTINITIALIZED)
        return fail(1, "initial_com_apartment");
    if (int rc = test_heap()) return rc;
    if (int rc = test_filetime()) return rc;
    if (int rc = test_array()) return rc;
    if (int rc = test_list()) return rc;
    if (::CoGetContextToken(&token) != CO_E_NOTINITIALIZED)
        return fail(2, "implicit_com_apartment");
    std::puts("ATL_EXTENSIONS_OK heap=1 filetime=1 array=1 list=1 no_com=1");
    return 0;
}
