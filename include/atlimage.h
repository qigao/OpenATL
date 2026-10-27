// OpenATL - Win32 CImage bitmap owner (C++17).
// ATL/RATL API compatibility; independently implemented with Win32 GDI.
// Intentionally no GDI+ codec integration, COM apartment, or COM server.
#ifndef __ATLIMAGE_H__
#define __ATLIMAGE_H__
#pragma once

#include "atldef.h"
#include <windows.h>
#include <cstddef>
#include <limits>

namespace ATL {

// Minimal CImage: one owned HBITMAP and at most one borrowed memory DC.
// The view returned by GetDC must not outlive ReleaseDC/Destroy/Detach.
// Created bitmaps are top-down DIB sections; 24/32 bpp are qualified.
class CImage {
public:
    enum { createAlphaChannel = 1 };
    enum DIBOrientation {
        DIBOR_DEFAULT, DIBOR_TOPDOWN, DIBOR_BOTTOMUP
    };

    CImage() noexcept = default;
    ~CImage() noexcept { Destroy(); }
    CImage(const CImage&) = delete;
    CImage& operator=(const CImage&) = delete;
    CImage(CImage&&) = delete;
    CImage& operator=(CImage&&) = delete;

    operator HBITMAP() const noexcept { return bitmap_; }
    bool IsNull() const noexcept { return bitmap_ == NULL; }
    int GetWidth() const noexcept { return width_; }
    int GetHeight() const noexcept { return height_; }
    int GetBPP() const noexcept { return bpp_; }
    int GetPitch() const noexcept { return pitch_; }
    bool HasAlphaChannel() const noexcept { return has_alpha_; }

    BOOL Create(int width, int height, int bpp, DWORD flags = 0) noexcept {
        if (bitmap_ != NULL || width <= 0 || height <= 0 ||
            (bpp != 24 && bpp != 32) ||
            (flags & ~static_cast<DWORD>(createAlphaChannel)) != 0 ||
            ((flags & createAlphaChannel) != 0 && bpp != 32))
            return FALSE;
        const size_t bitmapBits = static_cast<size_t>(width) * static_cast<size_t>(bpp);
        if (bitmapBits > (std::numeric_limits<size_t>::max)() - 31)
            return FALSE;
        const size_t pitch = ((bitmapBits + 31) / 32) * 4;
        if (pitch > static_cast<size_t>((std::numeric_limits<int>::max)()))
            return FALSE;
        if (static_cast<size_t>(height) > (std::numeric_limits<size_t>::max)() / pitch)
            return FALSE;

        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height; // Top-down: row zero is visual top.
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = static_cast<WORD>(bpp);
        info.bmiHeader.biCompression = BI_RGB;
        void* pixels = NULL;
        HBITMAP bitmap = ::CreateDIBSection(NULL, &info, DIB_RGB_COLORS,
                                            &pixels, NULL, 0);
        if (bitmap == NULL || pixels == NULL) {
            if (bitmap != NULL) (void)::DeleteObject(bitmap);
            return FALSE;
        }
        bitmap_ = bitmap;
        bits_ = static_cast<BYTE*>(pixels);
        width_ = width;
        height_ = height;
        bpp_ = bpp;
        pitch_ = static_cast<int>(pitch);
        bottom_up_ = false;
        has_alpha_ = (flags & createAlphaChannel) != 0;
        return TRUE;
    }

    // Accepts ownership of a valid GDI bitmap only when this owner is empty.
    // Standard compatible bitmaps have no GetBits/GetPixelAddress, but Draw
    // and GetDC still work. DIB sections expose pixel memory.
    void Attach(HBITMAP bitmap, DIBOrientation orientation = DIBOR_DEFAULT) noexcept {
        if (bitmap_ != NULL || bitmap == NULL) return;
        DIBSECTION dib{};
        const int size = ::GetObjectW(bitmap, sizeof(dib), &dib);
        if (size < static_cast<int>(sizeof(BITMAP)) ||
            dib.dsBm.bmWidth <= 0 || dib.dsBm.bmHeight <= 0 ||
            dib.dsBm.bmWidthBytes <= 0)
            return;
        bitmap_ = bitmap;
        width_ = dib.dsBm.bmWidth;
        height_ = dib.dsBm.bmHeight;
        bpp_ = dib.dsBm.bmBitsPixel * dib.dsBm.bmPlanes;
        pitch_ = dib.dsBm.bmWidthBytes;
        bits_ = size == static_cast<int>(sizeof(DIBSECTION))
                    ? static_cast<BYTE*>(dib.dsBm.bmBits) : NULL;
        bottom_up_ = (orientation == DIBOR_BOTTOMUP) ||
            (orientation == DIBOR_DEFAULT && bits_ != NULL &&
             dib.dsBmih.biHeight > 0);
        has_alpha_ = false;
    }

    HBITMAP Detach() noexcept {
        ReleaseDC();
        HBITMAP returned = bitmap_;
        Reset();
        return returned;
    }

    void Destroy() noexcept {
        ReleaseDC();
        if (bitmap_ != NULL)
            (void)::DeleteObject(bitmap_);
        Reset();
    }

    HDC GetDC() const noexcept {
        if (bitmap_ == NULL) return NULL;
        if (dc_ != NULL) return dc_;
        HDC dc = ::CreateCompatibleDC(NULL);
        if (dc == NULL) return NULL;
        HGDIOBJ previous = ::SelectObject(dc, bitmap_);
        if (previous == NULL || previous == HGDI_ERROR) {
            (void)::DeleteDC(dc);
            return NULL;
        }
        dc_ = dc;
        old_bitmap_ = previous;
        return dc_;
    }

    void ReleaseDC() const noexcept {
        if (dc_ != NULL) {
            (void)::SelectObject(dc_, old_bitmap_);
            (void)::DeleteDC(dc_);
            dc_ = NULL;
            old_bitmap_ = NULL;
        }
    }

    void* GetBits() noexcept { return bits_; }
    const void* GetBits() const noexcept { return bits_; }

    void* GetPixelAddress(int x, int y) noexcept {
        if (bits_ == NULL || x < 0 || y < 0 ||
            x >= width_ || y >= height_ || bpp_ <= 0 || bpp_ % 8 != 0)
            return NULL;
        const size_t row = static_cast<size_t>(bottom_up_ ? height_ - 1 - y : y);
        return bits_ + row * static_cast<size_t>(pitch_) +
               static_cast<size_t>(x) * static_cast<size_t>(bpp_ / 8);
    }
    const void* GetPixelAddress(int x, int y) const noexcept {
        return const_cast<CImage*>(this)->GetPixelAddress(x, y);
    }

    COLORREF GetPixel(int x, int y) const noexcept {
        if (x < 0 || y < 0 || x >= width_ || y >= height_) return CLR_INVALID;
        const bool acquired = dc_ == NULL;
        HDC dc = GetDC();
        if (!dc) return CLR_INVALID;
        const COLORREF result = ::GetPixel(dc, x, y);
        if (acquired) ReleaseDC();
        return result;
    }

    void SetPixel(int x, int y, COLORREF color) noexcept {
        if (x < 0 || y < 0 || x >= width_ || y >= height_) return;
        const bool acquired = dc_ == NULL;
        HDC dc = GetDC();
        if (dc != NULL) (void)::SetPixelV(dc, x, y, color);
        if (acquired) ReleaseDC();
    }

    BOOL Draw(HDC destination, int x, int y, int destWidth,
              int destHeight) const noexcept {
        if (destination == NULL || bitmap_ == NULL ||
            destWidth <= 0 || destHeight <= 0)
            return FALSE;
        const bool acquired = dc_ == NULL;
        HDC source = GetDC();
        if (!source) return FALSE;
        const BOOL ok = (destWidth == width_ && destHeight == height_) ?
            ::BitBlt(destination, x, y, destWidth, destHeight,
                     source, 0, 0, SRCCOPY) :
            ::StretchBlt(destination, x, y, destWidth, destHeight,
                         source, 0, 0, width_, height_, SRCCOPY);
        if (acquired) ReleaseDC();
        return ok;
    }

    BOOL Draw(HDC destination, int x, int y) const noexcept {
        return Draw(destination, x, y, width_, height_);
    }

private:
    void Reset() noexcept {
        bitmap_ = NULL;
        bits_ = NULL;
        width_ = height_ = bpp_ = pitch_ = 0;
        bottom_up_ = has_alpha_ = false;
    }

    HBITMAP bitmap_ = NULL;
    BYTE* bits_ = NULL;
    int width_ = 0;
    int height_ = 0;
    int bpp_ = 0;
    int pitch_ = 0;
    bool bottom_up_ = false;
    bool has_alpha_ = false;
    mutable HDC dc_ = NULL;
    mutable HGDIOBJ old_bitmap_ = NULL;
};

} // namespace ATL
#endif // __ATLIMAGE_H__
