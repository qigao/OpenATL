// OpenATL - FILETIME and time-span utilities (C++17).
// Public API shape informed by ATL / RATL's public-domain atltime.h.
// Independently implemented with checked Win32 FILETIME arithmetic.
#ifndef __ATLTIME_H__
#define __ATLTIME_H__
#pragma once

#include "atldef.h"
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <limits>

namespace ATL {

// A signed duration measured in Win32 FILETIME units (100 nanoseconds).
class CFileTimeSpan {
public:
    CFileTimeSpan() noexcept : ticks_(0) {}
    CFileTimeSpan(LONGLONG ticks) noexcept : ticks_(ticks) {}

    LONGLONG GetTimeSpan() const noexcept { return ticks_; }
    void SetTimeSpan(LONGLONG ticks) noexcept { ticks_ = ticks; }

    // Explicit checked operation for callers handling untrusted durations.
    bool TryAdd(CFileTimeSpan other, CFileTimeSpan* out) const noexcept {
        if (out == nullptr)
            return false;
        const LONGLONG v = other.ticks_;
        if ((v > 0 && ticks_ > (std::numeric_limits<LONGLONG>::max)() - v) ||
            (v < 0 && ticks_ < (std::numeric_limits<LONGLONG>::min)() - v))
            return false;
        out->ticks_ = ticks_ + v;
        return true;
    }

    // Consistent with Win32/ATL integer-width arithmetic: wraps at 64 bits.
    // Prefer TryAdd when overflow must be rejected.
    CFileTimeSpan operator+(CFileTimeSpan b) const noexcept {
        const std::uint64_t bits = static_cast<std::uint64_t>(ticks_) +
                                   static_cast<std::uint64_t>(b.ticks_);
        LONGLONG signed_bits;
        std::memcpy(&signed_bits, &bits, sizeof(bits));
        return CFileTimeSpan(signed_bits);
    }
    CFileTimeSpan operator-(CFileTimeSpan b) const noexcept {
        const std::uint64_t bits = static_cast<std::uint64_t>(ticks_) -
                                   static_cast<std::uint64_t>(b.ticks_);
        LONGLONG signed_bits;
        std::memcpy(&signed_bits, &bits, sizeof(bits));
        return CFileTimeSpan(signed_bits);
    }
    CFileTimeSpan& operator+=(CFileTimeSpan b) noexcept { *this = *this + b; return *this; }
    CFileTimeSpan& operator-=(CFileTimeSpan b) noexcept { *this = *this - b; return *this; }

    bool operator==(CFileTimeSpan b) const noexcept { return ticks_ == b.ticks_; }
    bool operator!=(CFileTimeSpan b) const noexcept { return ticks_ != b.ticks_; }
    bool operator< (CFileTimeSpan b) const noexcept { return ticks_ < b.ticks_; }
    bool operator<=(CFileTimeSpan b) const noexcept { return ticks_ <= b.ticks_; }
    bool operator> (CFileTimeSpan b) const noexcept { return ticks_ > b.ticks_; }
    bool operator>=(CFileTimeSpan b) const noexcept { return ticks_ >= b.ticks_; }

private:
    LONGLONG ticks_;
};

// FILETIME-compatible layout for direct Win32 API consumption.
class CFileTime : public FILETIME {
public:
    static constexpr ULONGLONG Millisecond = 10000ULL;
    static constexpr ULONGLONG Second = Millisecond * 1000ULL;
    static constexpr ULONGLONG Minute = Second * 60ULL;
    static constexpr ULONGLONG Hour = Minute * 60ULL;
    static constexpr ULONGLONG Day = Hour * 24ULL;
    static constexpr ULONGLONG Week = Day * 7ULL;

    CFileTime() noexcept { SetTime(0); }
    CFileTime(const FILETIME& ft) noexcept {
        dwLowDateTime = ft.dwLowDateTime;
        dwHighDateTime = ft.dwHighDateTime;
    }
    CFileTime(ULONGLONG ticks) noexcept { SetTime(ticks); }

    static CFileTime GetCurrentTime() noexcept {
        FILETIME value{};
        ::GetSystemTimeAsFileTime(&value);
        return CFileTime(value);
    }

    ULONGLONG GetTime() const noexcept {
        return (static_cast<ULONGLONG>(dwHighDateTime) << 32) |
               static_cast<ULONGLONG>(dwLowDateTime);
    }
    void SetTime(ULONGLONG ticks) noexcept {
        dwLowDateTime = static_cast<DWORD>(ticks);
        dwHighDateTime = static_cast<DWORD>(ticks >> 32);
    }
    CFileTime& operator=(const FILETIME& ft) noexcept {
        dwLowDateTime = ft.dwLowDateTime;
        dwHighDateTime = ft.dwHighDateTime;
        return *this;
    }

    // Reject overflow and underflow without modifying the output.
    bool TryAdd(CFileTimeSpan span, CFileTime* out) const noexcept {
        if (out == nullptr)
            return false;
        const ULONGLONG origin = GetTime();
        const LONGLONG delta = span.GetTimeSpan();
        if (delta >= 0) {
            const ULONGLONG add = static_cast<ULONGLONG>(delta);
            if (origin > (std::numeric_limits<ULONGLONG>::max)() - add)
                return false;
            out->SetTime(origin + add);
        } else {
            const ULONGLONG subtract = 0ULL - static_cast<ULONGLONG>(delta);
            if (origin < subtract)
                return false;
            out->SetTime(origin - subtract);
        }
        return true;
    }

    CFileTime operator+(CFileTimeSpan span) const noexcept {
        return CFileTime(GetTime() + static_cast<ULONGLONG>(span.GetTimeSpan()));
    }
    CFileTime operator-(CFileTimeSpan span) const noexcept {
        return CFileTime(GetTime() - static_cast<ULONGLONG>(span.GetTimeSpan()));
    }
    CFileTime& operator+=(CFileTimeSpan span) noexcept { *this = *this + span; return *this; }
    CFileTime& operator-=(CFileTimeSpan span) noexcept { *this = *this - span; return *this; }
    // Returns the signed 100ns difference. Wraps at 64 bits for extreme
    // deltas, just like RATL/Win32; ordinary file times use a bounded range.
    CFileTimeSpan operator-(CFileTime rhs) const noexcept {
        const ULONGLONG diff = GetTime() - rhs.GetTime();
        LONGLONG result;
        std::memcpy(&result, &diff, sizeof(diff));
        return CFileTimeSpan(result);
    }

    bool operator==(CFileTime b) const noexcept { return GetTime() == b.GetTime(); }
    bool operator!=(CFileTime b) const noexcept { return GetTime() != b.GetTime(); }
    bool operator< (CFileTime b) const noexcept { return GetTime() < b.GetTime(); }
    bool operator<=(CFileTime b) const noexcept { return GetTime() <= b.GetTime(); }
    bool operator> (CFileTime b) const noexcept { return GetTime() > b.GetTime(); }
    bool operator>=(CFileTime b) const noexcept { return GetTime() >= b.GetTime(); }
};

} // namespace ATL
#endif // __ATLTIME_H__
