// OpenATL - Win32 CImage bitmap owner (C++17).
// ATL/RATL API compatibility; independently implemented with Win32 GDI.
// Optional file codecs use Win32 GDI+ without a COM apartment or COM server.
#ifndef __ATLIMAGE_H__
#define __ATLIMAGE_H__
#pragma once

#include "atldef.h"
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <cstddef>
#include <cwchar>
#include <cstring>
#include <limits>
#include <memory>
#include <new>

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
        // Bound all owned bitmap pixel allocations, including plain GDI
        // Create. High dimensions must fail before CreateDIBSection.
        const ULONGLONG pixels = static_cast<ULONGLONG>(width) *
                                 static_cast<ULONGLONG>(height);
        if (pixels > kMaximumDecodedBytes / static_cast<ULONGLONG>(bpp / 8))
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


    // Decode a BMP/PNG file without transferring an HBITMAP through GDI+.
    // Copies full BGRA scanlines into an owned top-down Win32 DIB, including
    // per-pixel alpha. On failure *this remains empty. Does not open a COM
    // apartment, register codecs, or retain process-wide GDI+ state.
    HRESULT Load(LPCTSTR filename) noexcept {
        if (filename == NULL || *filename == 0)
            return E_INVALIDARG;
        if (bitmap_ != NULL)
            return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
        // Reject a known oversized encoded file before initializing GDI+.
        // The same 64 MiB policy applies to file paths and borrowed streams.
        WIN32_FILE_ATTRIBUTE_DATA fileInfo{};
        if (::GetFileAttributesExW(filename, GetFileExInfoStandard, &fileInfo)) {
            const ULONGLONG length =
                (static_cast<ULONGLONG>(fileInfo.nFileSizeHigh) << 32) |
                static_cast<ULONGLONG>(fileInfo.nFileSizeLow);
            if (length > kMaximumEncodedBytes)
                return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        }
        CodecSession session;
        if (!session.active())
            return CodecHRESULT(session.status);
        Gdiplus::Bitmap decoded(filename, FALSE);
        return DecodeBitmap(decoded);
    }

    // Borrow a caller-owned stream at its current read position. Snapshot
    // the BMP/PNG bytes into an OS HGLOBAL stream before GDI+ touches them:
    // COM short S_OK reads are rejected, failed HRESULTs are returned
    // unchanged, and memory input is capped at 64 MiB. No COM apartment
    // or application-owned COM Server is created. The caller still owns
    // its stream, while this CImage owns only the resulting HBITMAP.
    HRESULT Load(IStream* stream) noexcept {
        if (stream == NULL)
            return E_POINTER;
        if (bitmap_ != NULL)
            return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
        struct InputOwner {
            IStream* p = NULL;
            ~InputOwner() { if (p != NULL) p->Release(); }
        } staged;
        HRESULT hr = SnapshotEncodedInput(stream, &staged.p);
        if (FAILED(hr))
            return hr;
        CodecSession session;
        if (!session.active())
            return CodecHRESULT(session.status);
        Gdiplus::Bitmap decoded(staged.p, FALSE);
        return DecodeBitmap(decoded);
    }

    HRESULT Load(std::nullptr_t) noexcept { return E_INVALIDARG; }

    // Lossless BMP and PNG file encoding. An explicit GDI+ ImageFormat
    // GUID may be supplied in place of the extension. No JPEG/GIF/TIFF
    // codec is advertised yet. Requires a DIB with accessible pixel bits.
    // A scratch BGRA/RGB buffer avoids GDI+'s HBITMAP conversion, which
    // otherwise discards alpha. Unsupported/invalid inputs do not mutate
    // the owned CImage state or create an output file.
    HRESULT Save(LPCTSTR filename, REFGUID fileType = GUID_NULL) const noexcept {
        if (filename == NULL || *filename == 0)
            return E_INVALIDARG;
        if (bitmap_ == NULL)
            return E_HANDLE;
        if (bits_ == NULL || (bpp_ != 24 && bpp_ != 32) ||
            width_ <= 0 || height_ <= 0 || pitch_ <= 0)
            return E_NOTIMPL;

        const wchar_t* mime = NULL;
        if (::IsEqualGUID(fileType, GUID_NULL)) {
            const wchar_t* extension = std::wcsrchr(filename, L'.');
            if (extension != NULL && ::_wcsicmp(extension, L".png") == 0)
                mime = L"image/png";
            else if (extension != NULL && ::_wcsicmp(extension, L".bmp") == 0)
                mime = L"image/bmp";
        } else if (::IsEqualGUID(fileType, Gdiplus::ImageFormatPNG)) {
            mime = L"image/png";
        } else if (::IsEqualGUID(fileType, Gdiplus::ImageFormatBMP)) {
            mime = L"image/bmp";
        }
        if (mime == NULL)
            return E_INVALIDARG;

        return EncodeBitmap(filename, NULL, mime);
    }

    // The stream must already exist and remains caller-owned. The encoder
    // writes to its current position; no stream is closed or retained.
    // Explicit file-type pointer is mandatory (BMP/PNG only).
    HRESULT Save(IStream* stream, const GUID* fileType) const noexcept {
        if (stream == NULL || fileType == NULL)
            return E_POINTER;
        if (bitmap_ == NULL)
            return E_HANDLE;
        if (bits_ == NULL || (bpp_ != 24 && bpp_ != 32) ||
            width_ <= 0 || height_ <= 0 || pitch_ <= 0)
            return E_NOTIMPL;
        const wchar_t* mime = NULL;
        if (::IsEqualGUID(*fileType, Gdiplus::ImageFormatPNG))
            mime = L"image/png";
        else if (::IsEqualGUID(*fileType, Gdiplus::ImageFormatBMP))
            mime = L"image/bmp";
        if (mime == NULL)
            return E_INVALIDARG;
        return EncodeBitmap(NULL, stream, mime);
    }


private:
    static constexpr ULONGLONG kMaximumEncodedBytes =
        64ULL * 1024ULL * 1024ULL;
    static constexpr ULONGLONG kMaximumDecodedBytes =
        128ULL * 1024ULL * 1024ULL;

    // Consumes a fully constructed GDI+ image and commits only after a
    // successful copy. Both file and IStream decoding use this exact path.
    HRESULT DecodeBitmap(Gdiplus::Bitmap& decoded) noexcept {
        Gdiplus::Status status = decoded.GetLastStatus();
        if (status != Gdiplus::Ok)
            return CodecHRESULT(status);
        GUID actualFormat{};
        status = decoded.GetRawFormat(&actualFormat);
        if (status != Gdiplus::Ok)
            return CodecHRESULT(status);
        // Decode only the formats this OpenATL subset explicitly supports.
        // GDI+ may recognize JPEG/GIF/TIFF; that does not expand our API.
        if (!::IsEqualGUID(actualFormat, Gdiplus::ImageFormatBMP) &&
            !::IsEqualGUID(actualFormat, Gdiplus::ImageFormatPNG))
            return E_NOTIMPL;
        const UINT width = decoded.GetWidth();
        const UINT height = decoded.GetHeight();
        if (width == 0 || height == 0 ||
            width > static_cast<UINT>((std::numeric_limits<int>::max)()) ||
            height > static_cast<UINT>((std::numeric_limits<int>::max)()))
            return E_INVALIDARG;
        // Limit decoded pixel memory independently from the 64 MiB
        // compressed IStream limit. A tiny compressed image can otherwise
        // request a much larger DIB and GDI+ LockBits allocation.
        if (static_cast<ULONGLONG>(width) * static_cast<ULONGLONG>(height) >
            kMaximumDecodedBytes / 4ULL)
            return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
        const bool alpha =
            (static_cast<UINT>(decoded.GetPixelFormat()) &
             static_cast<UINT>(PixelFormatAlpha)) != 0;

        CImage temporary;
        if (!temporary.Create(static_cast<int>(width), static_cast<int>(height),
                              32, alpha ? createAlphaChannel : 0))
            return E_OUTOFMEMORY;

        Gdiplus::Rect bounds(0, 0, static_cast<INT>(width),
                                   static_cast<INT>(height));
        Gdiplus::BitmapData view{};
        status = decoded.LockBits(&bounds, Gdiplus::ImageLockModeRead,
                                  PixelFormat32bppARGB, &view);
        if (status != Gdiplus::Ok)
            return CodecHRESULT(status);

        HRESULT copyResult = S_OK;
        if (view.Scan0 == NULL || view.Stride == 0 ||
            width > (std::numeric_limits<size_t>::max)() / 4) {
            copyResult = E_FAIL;
        } else {
            const size_t rowBytes = static_cast<size_t>(width) * 4;
            const size_t stride = view.Stride >= 0 ?
                static_cast<size_t>(view.Stride) :
                static_cast<size_t>(-static_cast<long long>(view.Stride));
            if (stride < rowBytes)
                copyResult = E_FAIL;
            else {
                for (UINT y = 0; y < height; ++y) {
                    BYTE* target = static_cast<BYTE*>(
                        temporary.GetPixelAddress(0, static_cast<int>(y)));
                    const BYTE* source =
                        static_cast<const BYTE*>(view.Scan0) +
                        static_cast<std::ptrdiff_t>(y) * view.Stride;
                    if (target == NULL) {
                        copyResult = E_FAIL;
                        break;
                    }
                    std::memcpy(target, source, rowBytes);
                }
            }
        }
        const Gdiplus::Status unlock = decoded.UnlockBits(&view);
        if (FAILED(copyResult))
            return copyResult;
        if (unlock != Gdiplus::Ok)
            return CodecHRESULT(unlock);

        HBITMAP transferred = temporary.Detach();
        Attach(transferred, DIBOR_TOPDOWN);
        if (bitmap_ == NULL) {
            (void)::DeleteObject(transferred);
            return E_FAIL;
        }
        has_alpha_ = alpha;
        return S_OK;
    }

    // Files and caller-owned IStreams share the same pixel snapshot,
    // encoder selection, startup/shutdown and failure cleanup.
    HRESULT EncodeBitmap(const wchar_t* filename, IStream* stream,
                         const wchar_t* mime) const noexcept {
        const size_t height = static_cast<size_t>(height_);
        const size_t pitch = static_cast<size_t>(pitch_);
        if (height > (std::numeric_limits<size_t>::max)() / pitch)
            return E_INVALIDARG;
        const size_t bytes = height * pitch;
        const size_t rowBytes = static_cast<size_t>(width_) *
                                static_cast<size_t>(bpp_ / 8);
        if (rowBytes > pitch)
            return E_INVALIDARG;
        // A 24-bpp DIB may have up to three padding bytes per row.
        // Initialize the entire staging area, including row padding,
        // so the BMP encoder cannot serialize stale allocator contents.
        std::unique_ptr<BYTE[]> pixels(new (std::nothrow) BYTE[bytes]());
        if (!pixels)
            return E_OUTOFMEMORY;
        if (dc_ != NULL)
            (void)::GdiFlush();
        for (int y = 0; y < height_; ++y) {
            const BYTE* source = static_cast<const BYTE*>(
                GetPixelAddress(0, y));
            if (source == NULL)
                return E_FAIL;
            std::memcpy(pixels.get() + static_cast<size_t>(y) * pitch,
                        source, rowBytes);
        }

        CodecSession session;
        if (!session.active())
            return CodecHRESULT(session.status);
        CLSID encoder{};
        HRESULT hr = EncoderForMime(mime, &encoder);
        if (FAILED(hr))
            return hr;

        const Gdiplus::PixelFormat format = bpp_ == 24 ?
            PixelFormat24bppRGB :
            (has_alpha_ ? PixelFormat32bppARGB :
                          PixelFormat32bppRGB);
        // Stage the entire encoding in an OS-owned memory stream before
        // invoking any caller-supplied IStream::Write. GDI+ does not
        // reliably expose a short S_OK write as an HRESULT. Copying the
        // completed encoding ourselves lets us fail closed on short
        // writes, zero-byte writes and failed HRESULTs.
        struct ScratchStream {
            IStream* p = NULL;
            ~ScratchStream() { if (p != NULL) p->Release(); }
        } scratch;
        if (stream != NULL) {
            hr = ::CreateStreamOnHGlobal(NULL, TRUE, &scratch.p);
            if (FAILED(hr))
                return hr;
            if (scratch.p == NULL)
                return E_FAIL;
        }

        Gdiplus::Bitmap image(width_, height_, pitch_, format, pixels.get());
        const Gdiplus::Status constructed = image.GetLastStatus();
        if (constructed != Gdiplus::Ok)
            return CodecHRESULT(constructed);
        const Gdiplus::Status written = stream != NULL ?
            image.Save(scratch.p, &encoder, NULL) :
            image.Save(filename, &encoder, NULL);
        if (written != Gdiplus::Ok)
            return CodecHRESULT(written);
        if (stream == NULL)
            return S_OK;
        return CopyEncodedStream(scratch.p, stream);
    }

    // Some OS IStreams (notably CreateStreamOnHGlobal) return S_OK
    // with a short read at EOF. Accept this only if Stat and a
    // position-preserving Seek(CUR, 0) prove the read pointer is at the
    // stream's logical end. Custom short-S_OK streams lacking that
    // evidence fail closed, instead of silently truncating encoded data.
    static bool ConfirmSourceEOF(IStream* source) noexcept {
        if (source == NULL)
            return false;
        STATSTG info{};
        if (FAILED(source->Stat(&info, STATFLAG_NONAME)))
            return false;
        LARGE_INTEGER zero{};
        ULARGE_INTEGER position{};
        if (FAILED(source->Seek(zero, STREAM_SEEK_CUR, &position)))
            return false;
        return position.QuadPart == info.cbSize.QuadPart;
    }

    // Snapshot caller-owned bytes before decoding. Source can advance,
    // including on failure; this is not a transactional read. The only
    // Seek used on source is a zero-offset position query for short
    // successful EOF, which never intentionally changes its position.
    static HRESULT SnapshotEncodedInput(IStream* source,
                                         IStream** output) noexcept {
        if (source == NULL || output == NULL)
            return E_POINTER;
        *output = NULL;
        IStream* staged = NULL;
        HRESULT hr = ::CreateStreamOnHGlobal(NULL, TRUE, &staged);
        if (FAILED(hr))
            return hr;
        if (staged == NULL)
            return E_FAIL;

        // If the source supports Stat + Seek(CUR, 0), reject a known
        // oversized remaining range before allocating even one read chunk.
        // Unseekable streams still work and are bounded by the byte counter.
        STATSTG bounds{};
        if (source->Stat(&bounds, STATFLAG_NONAME) == S_OK) {
            LARGE_INTEGER zero{};
            ULARGE_INTEGER position{};
            if (source->Seek(zero, STREAM_SEEK_CUR, &position) == S_OK) {
                if (position.QuadPart > bounds.cbSize.QuadPart) {
                    staged->Release();
                    return STG_E_READFAULT;
                }
                if (bounds.cbSize.QuadPart - position.QuadPart >
                    kMaximumEncodedBytes) {
                    staged->Release();
                    return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
                }
            }
        }
        ULONGLONG accumulated = 0;
        BYTE buffer[8192];
        for (;;) {
            ULONG read = 0;
            hr = source->Read(buffer, static_cast<ULONG>(sizeof(buffer)), &read);
            if (FAILED(hr)) {
                staged->Release();
                return hr;
            }
            if ((hr != S_OK && hr != S_FALSE) ||
                read > sizeof(buffer)) {
                staged->Release();
                return STG_E_READFAULT;
            }
            if (hr == S_OK && read != sizeof(buffer)) {
                if (!ConfirmSourceEOF(source)) {
                    staged->Release();
                    return STG_E_READFAULT;
                }
                // Normalize the proven EOF to the standard S_FALSE path.
                hr = S_FALSE;
            }
            if (read > kMaximumEncodedBytes - accumulated) {
                staged->Release();
                return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
            }
            if (read != 0) {
                ULONG written = 0;
                const HRESULT copy = staged->Write(buffer, read, &written);
                if (FAILED(copy) || written != read) {
                    staged->Release();
                    return FAILED(copy) ? copy : STG_E_WRITEFAULT;
                }
                accumulated += read;
            }
            if (hr == S_FALSE)
                break;
        }
        if (accumulated == 0) {
            staged->Release();
            return STG_E_READFAULT;
        }
        LARGE_INTEGER zero{};
        hr = staged->Seek(zero, STREAM_SEEK_SET, NULL);
        if (FAILED(hr)) {
            staged->Release();
            return hr;
        }
        *output = staged;
        return S_OK;
    }

    // This is a client-side transfer of an already encoded image, not a COM
    // object/server implementation. The destination is borrowed, never
    // released or sought, and remains at the first unwritten byte if its
    // Write fails. Arbitrary streams cannot guarantee atomic rollback.
    static HRESULT CopyEncodedStream(IStream* encoded,
                                     IStream* destination) noexcept {
        if (encoded == NULL || destination == NULL)
            return E_POINTER;
        STATSTG stat{};
        HRESULT hr = encoded->Stat(&stat, STATFLAG_NONAME);
        if (FAILED(hr))
            return hr;
        ULONGLONG remaining = stat.cbSize.QuadPart;
        if (remaining == 0)
            return STG_E_READFAULT;
        LARGE_INTEGER start{};
        hr = encoded->Seek(start, STREAM_SEEK_SET, NULL);
        if (FAILED(hr))
            return hr;
        BYTE buffer[8192];
        while (remaining != 0) {
            const ULONG amount = remaining > sizeof(buffer) ?
                static_cast<ULONG>(sizeof(buffer)) :
                static_cast<ULONG>(remaining);
            ULONG read = 0;
            hr = encoded->Read(buffer, amount, &read);
            if (FAILED(hr))
                return hr;
            if (hr != S_OK || read != amount)
                return STG_E_READFAULT;

            ULONG written = 0;
            hr = destination->Write(buffer, amount, &written);
            if (FAILED(hr))
                return hr;
            if (hr != S_OK || written != amount)
                return STG_E_WRITEFAULT;
            remaining -= amount;
        }
        return S_OK;
    }

    struct CodecSession {
        ULONG_PTR token = 0;
        Gdiplus::Status status = Gdiplus::GenericError;
        CodecSession() noexcept {
            Gdiplus::GdiplusStartupInput input;
            // System built-in BMP/PNG codecs only; never load external
            // codec plug-ins into an otherwise COM-free Win32 UI process.
            input.SuppressExternalCodecs = TRUE;
            status = Gdiplus::GdiplusStartup(&token, &input, NULL);
            if (status != Gdiplus::Ok)
                token = 0;
        }
        ~CodecSession() {
            if (token != 0)
                Gdiplus::GdiplusShutdown(token);
        }
        CodecSession(const CodecSession&) = delete;
        CodecSession& operator=(const CodecSession&) = delete;
        bool active() const noexcept { return status == Gdiplus::Ok; }
    };

    static HRESULT CodecHRESULT(Gdiplus::Status status) noexcept {
        switch (status) {
        case Gdiplus::Ok: return S_OK;
        case Gdiplus::InvalidParameter: return E_INVALIDARG;
        case Gdiplus::OutOfMemory: return E_OUTOFMEMORY;
        case Gdiplus::FileNotFound: return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        case Gdiplus::AccessDenied: return E_ACCESSDENIED;
        case Gdiplus::UnknownImageFormat: return HRESULT_FROM_WIN32(ERROR_BAD_FORMAT);
        default: return E_FAIL;
        }
    }

    static HRESULT EncoderForMime(const wchar_t* mime, CLSID* output) noexcept {
        if (mime == NULL || output == NULL)
            return E_INVALIDARG;
        UINT count = 0;
        UINT length = 0;
        Gdiplus::Status status =
            Gdiplus::GetImageEncodersSize(&count, &length);
        if (status != Gdiplus::Ok)
            return CodecHRESULT(status);
        if (count == 0 || length == 0)
            return E_NOTIMPL;
        std::unique_ptr<BYTE[]> buffer(new (std::nothrow) BYTE[length]);
        if (!buffer)
            return E_OUTOFMEMORY;
        auto* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.get());
        status = Gdiplus::GetImageEncoders(count, length, codecs);
        if (status != Gdiplus::Ok)
            return CodecHRESULT(status);
        for (UINT i = 0; i < count; ++i) {
            if (codecs[i].MimeType != NULL &&
                std::wcscmp(codecs[i].MimeType, mime) == 0) {
                *output = codecs[i].Clsid;
                return S_OK;
            }
        }
        return E_NOTIMPL;
    }

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
