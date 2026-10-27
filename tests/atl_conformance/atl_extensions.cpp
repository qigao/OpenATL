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

int main() {
    static_assert(sizeof(void*) == 8, "Only Windows x64 and ARM64 qualified");
    ULONG_PTR token = 0;
    if (::CoGetContextToken(&token) != CO_E_NOTINITIALIZED)
        return fail(1, "initial_com_apartment");
    if (int rc = test_heap()) return rc;
    if (int rc = test_filetime()) return rc;
    if (int rc = test_array()) return rc;
    if (::CoGetContextToken(&token) != CO_E_NOTINITIALIZED)
        return fail(2, "implicit_com_apartment");
    std::puts("ATL_EXTENSIONS_OK heap=1 filetime=1 array=1 no_com=1");
    return 0;
}
