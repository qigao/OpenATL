// Native OpenATL CImage subset: Win32 DIB, GDI DC ownership, detach/
// attach, top-down and bottom-up row orientation, draw, and no COM.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <ole2.h>
#include <atlimage.h>
#include <cstdio>
#include <cstdint>
#include <cwchar>

#if !defined(OPENATL_REPOSITORY_HEADERS) || OPENATL_REPOSITORY_HEADERS != 0x4F41544CUL
#error "Image tests must compile checked-in OpenATL headers"
#endif

static int fail(int code, const char* phase) {
    std::fprintf(stderr, "ATL_IMAGE_FAIL code=%d phase=%s\n", code, phase);
    return code;
}

static int dib_and_draw() {
    ATL::CImage invalid;
    if (invalid.Create(0, 2, 32) ||
        invalid.Create(2, -2, 32) ||
        invalid.Create(2, 2, 8) ||
        invalid.Create(2, 2, 24, ATL::CImage::createAlphaChannel) ||
        !invalid.IsNull())
        return fail(11, "Reject_unsupported_DIB");

    // Create must enforce the owned DIB pixel budget without allocating.
    ATL::CImage oversized;
    if (oversized.Create(8193, 4096, 32) ||
        oversized.Create(16384, 4096, 24) ||
        oversized.Create(65536, 65536, 32) ||
        !oversized.IsNull() || oversized.GetWidth() != 0 ||
        oversized.GetBits() != nullptr)
        return fail(126, "Reject_decode_pixel_budget");
    ATL::CImage src;
    if (!src.Create(2, 2, 32, ATL::CImage::createAlphaChannel) ||
        src.IsNull() || src.GetWidth() != 2 || src.GetHeight() != 2 ||
        src.GetPitch() != 8 || src.GetBPP() != 32 ||
        !src.HasAlphaChannel() || src.GetBits() == nullptr)
        return fail(12, "Create_topdown_32bpp");
    if (src.Create(1, 1, 32) ||
        src.GetPixelAddress(-1, 0) != nullptr ||
        src.GetPixelAddress(2, 0) != nullptr ||
        src.GetPixelAddress(0, 2) != nullptr)
        return fail(13, "Reject_recreation_and_oob");

    src.SetPixel(0, 0, RGB(12, 34, 56));
    src.SetPixel(1, 0, RGB(100, 101, 102));
    src.SetPixel(0, 1, RGB(20, 30, 40));
    src.SetPixel(1, 1, RGB(80, 90, 100));
    if (src.GetPixel(0, 0) != RGB(12, 34, 56) ||
        src.GetPixel(1, 1) != RGB(80, 90, 100) ||
        src.GetPixel(-1, 0) != CLR_INVALID)
        return fail(14, "GDI_SetGetPixel");

    auto* top = static_cast<BYTE*>(src.GetPixelAddress(0, 0));
    auto* bottom = static_cast<BYTE*>(src.GetPixelAddress(0, 1));
    if (!top || !bottom || bottom - top != src.GetPitch() ||
        top[0] != 56 || top[1] != 34 || top[2] != 12)
        return fail(15, "Topdown_DIB_layout");
    top[3] = 0xFF;

    HDC held = src.GetDC();
    if (held == NULL || src.GetDC() != held)
        return fail(16, "Borrow_GetDC");
    src.ReleaseDC();
    HDC second = src.GetDC();
    if (second == NULL)
        return fail(17, "Reborrow_DC");

    ATL::CImage target;
    if (!target.Create(4, 4, 32))
        return fail(18, "Create_target");
    HDC destDC = target.GetDC();
    if (!destDC || !src.Draw(destDC, 0, 0, 4, 4))
        return fail(19, "StretchBlt");
    if (target.GetPixel(0, 0) != RGB(12, 34, 56) ||
        target.GetPixel(3, 3) != RGB(80, 90, 100))
        return fail(20, "Draw_pixel_match");
    target.ReleaseDC();
    src.ReleaseDC();
    if (src.Draw(NULL, 0, 0) || src.Draw(destDC, 0, 0, 0, 2))
        return fail(21, "Reject_bad_draw");

    HBITMAP detached = src.Detach();
    if (!detached || !src.IsNull() || src.GetWidth() ||
        src.GetPixelAddress(0, 0) != nullptr || src.GetDC() != NULL)
        return fail(22, "Detach_owner");
    ATL::CImage owned;
    owned.Attach(detached);
    if (owned.IsNull() || owned.GetWidth() != 2 ||
        owned.GetPixel(1, 0) != RGB(100, 101, 102))
        return fail(23, "Attach_existing_DIB");
    owned.Destroy();
    if (!owned.IsNull() || owned.GetBits() || owned.GetDC() ||
        owned.GetWidth() != 0)
        return fail(24, "Destroy_owner");

    ATL::CImage aligned;
    if (!aligned.Create(2, 2, 24) || aligned.GetPitch() != 8 ||
        aligned.GetBPP() != 24 || aligned.HasAlphaChannel())
        return fail(25, "24bpp_pitch");
    aligned.SetPixel(1, 1, RGB(200, 50, 20));
    if (aligned.GetPixel(1, 1) != RGB(200, 50, 20))
        return fail(26, "24bpp_color");
    return 0;
}

static int bottom_up_attach() {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 2;
    info.bmiHeader.biHeight = 2; // Positive height: bottom-up DIB.
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = NULL;
    HBITMAP bitmap = ::CreateDIBSection(NULL, &info, DIB_RGB_COLORS,
                                        &pixels, NULL, 0);
    if (!bitmap || !pixels) {
        if (bitmap) (void)::DeleteObject(bitmap);
        return fail(31, "Create_bottomup_DIB");
    }
    ATL::CImage image;
    image.Attach(bitmap);
    if (image.IsNull() || image.GetWidth() != 2 || image.GetHeight() != 2)
        return fail(32, "Attach_bottomup");
    BYTE* raw = static_cast<BYTE*>(image.GetBits());
    BYTE* logicalTop = static_cast<BYTE*>(image.GetPixelAddress(0, 0));
    BYTE* logicalBottom = static_cast<BYTE*>(image.GetPixelAddress(0, 1));
    if (!raw || !logicalTop || !logicalBottom ||
        logicalBottom != raw || logicalTop != raw + image.GetPitch())
        return fail(33, "DIB_bottomup_orientation");
    image.SetPixel(0, 0, RGB(7, 8, 9));
    if (image.GetPixel(0, 0) != RGB(7, 8, 9) ||
        logicalTop[0] != 9 || logicalTop[1] != 8 || logicalTop[2] != 7)
        return fail(34, "Bottomup_GDI_color");
    image.Destroy();
    return 0;
}

// Independent actual codec I/O, not a hand-authored PNG decoder. Bitmap
// source pixels are provided in BGRA order; nonopaque alpha is preserved.
struct TempImages {
    wchar_t png[MAX_PATH]{};
    wchar_t bmp[MAX_PATH]{};
    wchar_t unknown[MAX_PATH]{};
    wchar_t corrupt[MAX_PATH]{};
    wchar_t gif[MAX_PATH]{};
    ~TempImages() {
        if (png[0]) (void)::DeleteFileW(png);
        if (bmp[0]) (void)::DeleteFileW(bmp);
        if (unknown[0]) (void)::DeleteFileW(unknown);
        if (corrupt[0]) (void)::DeleteFileW(corrupt);
        if (gif[0]) (void)::DeleteFileW(gif);
    }
};

static bool make_temp_paths(TempImages* paths) {
    wchar_t directory[MAX_PATH]{};
    DWORD len = ::GetTempPathW(MAX_PATH, directory);
    if (len == 0 || len >= MAX_PATH) return false;
    wchar_t temporary[MAX_PATH]{};
    if (!::GetTempFileNameW(directory, L"OAI", 0, temporary))
        return false;
    (void)::DeleteFileW(temporary);
    return std::swprintf(paths->png, MAX_PATH, L"%ls.png", temporary) > 0 &&
           std::swprintf(paths->bmp, MAX_PATH, L"%ls.bmp", temporary) > 0 &&
           std::swprintf(paths->unknown, MAX_PATH, L"%ls.dat", temporary) > 0 &&
           std::swprintf(paths->corrupt, MAX_PATH, L"%ls.bad", temporary) > 0 &&
           std::swprintf(paths->gif, MAX_PATH, L"%ls.gif", temporary) > 0;
}

static bool signature(const wchar_t* path, const BYTE* prefix, DWORD count) {
    HANDLE file = ::CreateFileW(path, GENERIC_READ, FILE_SHARE_READ,
                                 NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return false;
    BYTE bytes[8]{};
    DWORD read = 0;
    const BOOL ok = ::ReadFile(file, bytes, count, &read, NULL);
    (void)::CloseHandle(file);
    if (!ok || read != count) return false;
    for (DWORD i = 0; i < count; ++i)
        if (bytes[i] != prefix[i]) return false;
    return true;
}

// Inspect the actual BMP file payload, not merely its decoded pixels.
// For 24bpp 3-pixel rows, the final three bytes are unused row padding
// and must be deterministic zeros even if the source DIB padding is poisoned.
static bool bmp_row_padding_is_zero(const wchar_t* path) {
    HANDLE file = ::CreateFileW(path, GENERIC_READ, FILE_SHARE_READ,
                                NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return false;
    BITMAPFILEHEADER fh{};
    BITMAPINFOHEADER bh{};
    DWORD read = 0;
    bool valid = ::ReadFile(file, &fh, sizeof(fh), &read, NULL) &&
                 read == sizeof(fh) && fh.bfType == 0x4D42 &&
                 ::ReadFile(file, &bh, sizeof(bh), &read, NULL) &&
                 read == sizeof(bh) &&
                 bh.biWidth == 3 &&
                 (bh.biHeight == 2 || bh.biHeight == -2) &&
                 bh.biBitCount == 24 && bh.biCompression == BI_RGB;
    if (valid) {
        LARGE_INTEGER pos{};
        pos.QuadPart = fh.bfOffBits;
        BYTE pixels[24]{};
        valid = ::SetFilePointerEx(file, pos, NULL, FILE_BEGIN) &&
                ::ReadFile(file, pixels, sizeof(pixels), &read, NULL) &&
                read == sizeof(pixels);
        if (valid)
            valid = pixels[9] == 0 && pixels[10] == 0 &&
                    pixels[11] == 0 && pixels[21] == 0 &&
                    pixels[22] == 0 && pixels[23] == 0;
    }
    (void)::CloseHandle(file);
    return valid;
}

static int codec_roundtrip() {
    TempImages paths;
    if (!make_temp_paths(&paths))
        return fail(41, "temp_paths");

    ATL::CImage empty;
    if (empty.Load(nullptr) != E_INVALIDARG ||
        empty.Load(L"") != E_INVALIDARG ||
        empty.Save(paths.png) != E_HANDLE ||
        !empty.IsNull())
        return fail(42, "load_save_empty_inputs");

    ATL::CImage source;
    if (!source.Create(2, 2, 32, ATL::CImage::createAlphaChannel))
        return fail(43, "codec_source_dib");
    // Nonpremultiplied BGRA, including translucent pixels.
    const BYTE colors[4][4] = {
        {56, 34, 12, 255},
        {30, 20, 10, 128},
        {90, 100, 110, 255},
        {40, 50, 60, 200}
    };
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            BYTE* pixel = static_cast<BYTE*>(source.GetPixelAddress(x, y));
            if (!pixel) return fail(44, "codec_source_pixel");
            for (int b = 0; b < 4; ++b)
                pixel[b] = colors[y * 2 + x][b];
        }
    }

    if (source.Save(nullptr) != E_INVALIDARG ||
        source.Save(L"") != E_INVALIDARG ||
        source.Save(paths.unknown) != E_INVALIDARG ||
        ::GetFileAttributesW(paths.unknown) != INVALID_FILE_ATTRIBUTES)
        return fail(45, "reject_bad_save_extensions");

    HRESULT hr = source.Save(paths.png);
    if (FAILED(hr)) return fail(46, "png_save");
    const BYTE pngSignature[8]{137,80,78,71,13,10,26,10};
    if (!signature(paths.png, pngSignature, 8))
        return fail(47, "png_file_signature");
    ATL::CImage loaded;
    hr = loaded.Load(paths.png);
    if (FAILED(hr) || loaded.IsNull() || !loaded.HasAlphaChannel() ||
        loaded.GetWidth() != 2 || loaded.GetHeight() != 2 ||
        loaded.GetBPP() != 32)
        return fail(48, "png_load");
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            const BYTE* actual = static_cast<const BYTE*>(
                loaded.GetPixelAddress(x, y));
            if (!actual) return fail(49, "png_pixel_address");
            for (int b = 0; b < 4; ++b)
                if (actual[b] != colors[y * 2 + x][b])
                    return fail(50, "png_rgba_roundtrip");
        }
    }
    if (loaded.Load(paths.png) != HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS))
        return fail(51, "reject_load_over_existing_image");
    if (source.GetWidth() != 2 || !source.HasAlphaChannel() ||
        loaded.GetPixelAddress(1, 1) == nullptr)
        return fail(52, "preserve_on_failure");

    // BMP roundtrip uses opaque 24bpp. A requested explicit encoder GUID
    // works even when the path's extension does not identify the codec.
    ATL::CImage rgb;
    if (!rgb.Create(3, 2, 24)) return fail(53, "bmp_source_dib");
    rgb.SetPixel(0, 0, RGB(12, 34, 56));
    rgb.SetPixel(2, 1, RGB(123, 111, 99));
    // Poison only the *unowned* padding slots: Save must not preserve
    // these bytes in the encoded output, even when the source is dirty.
    BYTE* original = static_cast<BYTE*>(rgb.GetBits());
    if (!original || rgb.GetPitch() != 12) return fail(94, "BMP_padding_layout");
    for (int row = 0; row < 2; ++row)
        for (int byte = 9; byte < 12; ++byte)
            original[row * rgb.GetPitch() + byte] = 0xA7;
    hr = rgb.Save(paths.bmp);
    if (FAILED(hr)) return fail(54, "bmp_save_from_extension");
    if (!bmp_row_padding_is_zero(paths.bmp))
        return fail(95, "BMP_row_padding_must_be_zero");
    const BYTE bmpSignature[2]{'B', 'M'};
    if (!signature(paths.bmp, bmpSignature, 2))
        return fail(55, "bmp_file_signature");
    ATL::CImage decodedBMP;
    hr = decodedBMP.Load(paths.bmp);
    if (FAILED(hr) || decodedBMP.GetWidth() != 3 ||
        decodedBMP.GetHeight() != 2 ||
        decodedBMP.GetPixel(0, 0) != RGB(12, 34, 56) ||
        decodedBMP.GetPixel(2, 1) != RGB(123, 111, 99))
        return fail(56, "bmp_roundtrip");

    // The caller can override an unsupported extension with an explicit
    // encoder format GUID; decoding must inspect the actual image bytes.
    hr = rgb.Save(paths.unknown, Gdiplus::ImageFormatBMP);
    if (FAILED(hr) || !signature(paths.unknown, bmpSignature, 2))
        return fail(63, "explicit_guid_overrides_extension");
    ATL::CImage transcoded;
    hr = transcoded.Load(paths.unknown);
    if (FAILED(hr) || transcoded.GetPixel(2, 1) != RGB(123, 111, 99))
        return fail(64, "decode_disguised_bmp");

    // Unsupported GUID or nonexistent destination must fail without
    // destroying the caller-owned image.
    hr = source.Save(paths.unknown, GUID_NULL); // No extension-based encoder.
    if (hr != E_INVALIDARG || source.GetWidth() != 2)
        return fail(57, "save_unsupported_extension");
    const GUID unsupported = {0x701fca51,0x2e2b,0x4f32,{0x80,0x11,0x99,0x31,0x41,0x22,0x6e,0x55}};
    if (source.Save(paths.unknown, unsupported) != E_INVALIDARG)
        return fail(58, "save_unsupported_codec_guid");

    // Malformed bytes must not leave a partially initialized HBITMAP.
    HANDLE corrupt = ::CreateFileW(paths.corrupt, GENERIC_WRITE, 0, NULL,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (corrupt == INVALID_HANDLE_VALUE) return fail(59, "create_bad_file");
    const BYTE invalidBytes[] = {0, 1, 2, 3, 4, 5, 6, 7};
    DWORD written = 0;
    const BOOL wrote = ::WriteFile(corrupt, invalidBytes,
                                  sizeof(invalidBytes), &written, NULL);
    (void)::CloseHandle(corrupt);
    if (!wrote || written != sizeof(invalidBytes))
        return fail(60, "write_bad_file");

    ATL::CImage rejected;
    hr = rejected.Load(paths.corrupt);
    if (SUCCEEDED(hr) || !rejected.IsNull() || rejected.GetBits() != nullptr ||
        rejected.GetWidth() != 0)
        return fail(61, "corrupt_png_cleanup");
    hr = rejected.Load(L"Z:\\OpenATL-nonexistent-folder\\unreadable.png");
    if (SUCCEEDED(hr) || !rejected.IsNull())
        return fail(62, "missing_file_cleanup");

    // GDI+ itself can decode GIF, but this bounded OpenATL API must
    // reject it instead of silently expanding support beyond BMP/PNG.
    static constexpr BYTE smallGif[] = {
        0x47,0x49,0x46,0x38,0x39,0x61,0x01,0x00,0x01,0x00,
        0x80,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,
        0x21,0xf9,0x04,0x01,0x00,0x00,0x00,0x00,
        0x2c,0x00,0x00,0x00,0x00,0x01,0x00,0x01,0x00,0x00,
        0x02,0x02,0x44,0x01,0x00,0x3b
    };
    HANDLE gifFile = ::CreateFileW(paths.gif, GENERIC_WRITE, 0, NULL,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (gifFile == INVALID_HANDLE_VALUE)
        return fail(65, "Create_gif_test_data");
    written = 0;
    const BOOL gifWritten = ::WriteFile(gifFile, smallGif,
                     static_cast<DWORD>(sizeof(smallGif)), &written, NULL);
    (void)::CloseHandle(gifFile);
    if (!gifWritten || written != sizeof(smallGif))
        return fail(66, "Write_gif_test_data");
    ATL::CImage unsupportedDecoder;
    hr = unsupportedDecoder.Load(paths.gif);
    if (hr != E_NOTIMPL || !unsupportedDecoder.IsNull())
        return fail(67, "Reject_GDIplus_GIF_decoder");

    return 0;
}


// CreateStreamOnHGlobal supplies a real system-owned COM utility object.
// It can be used without initializing a COM apartment; OpenATL creates no
// application COM object/factory/server and never owns caller stream handles.
struct OwnedStream {
    IStream* ptr = nullptr;
    ~OwnedStream() { if (ptr != nullptr) ptr->Release(); }
    OwnedStream(const OwnedStream&) = delete;
    OwnedStream& operator=(const OwnedStream&) = delete;
    OwnedStream() = default;

    bool Create() { return SUCCEEDED(::CreateStreamOnHGlobal(NULL, TRUE, &ptr)) && ptr != nullptr; }
    bool Rewind() {
        if (ptr == nullptr) return false;
        LARGE_INTEGER offset{};
        return SUCCEEDED(ptr->Seek(offset, STREAM_SEEK_SET, nullptr));
    }
    bool Put(const BYTE* bytes, ULONG len) {
        if (ptr == nullptr || bytes == nullptr) return false;
        ULONG written = 0;
        return SUCCEEDED(ptr->Write(bytes, len, &written)) && written == len;
    }
    bool Peek(const BYTE* signature, ULONG len) {
        if (!Rewind()) return false;
        BYTE readBytes[8]{};
        if (len > sizeof(readBytes)) return false;
        ULONG got = 0;
        if (FAILED(ptr->Read(readBytes, len, &got)) || got != len)
            return false;
        for (ULONG i = 0; i < len; ++i)
            if (readBytes[i] != signature[i]) return false;
        return Rewind();
    }
};

static int stream_roundtrip() {
    ATL::CImage image;
    if (!image.Create(2, 2, 32, ATL::CImage::createAlphaChannel))
        return fail(70, "Create_image_for_IStream");

    const BYTE colors[4][4] = {
        {56, 34, 12, 255}, {30, 20, 10, 128},
        {90, 100, 110, 255}, {40, 50, 60, 200}
    };
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x) {
            BYTE* pixel = static_cast<BYTE*>(image.GetPixelAddress(x, y));
            if (!pixel) return fail(71, "IStream_source_DIB");
            for (int channel = 0; channel < 4; ++channel)
                pixel[channel] = colors[2 * y + x][channel];
        }

    OwnedStream encoded;
    if (!encoded.Create()) return fail(72, "CreateStreamOnHGlobal");
    const GUID invalidType = {0x96547593,0x1599,0x4bac,{0x86,0x35,0x0b,0xce,0xac,0x7a,0xa6,0x62}};
    if (image.Save(static_cast<IStream*>(nullptr), &Gdiplus::ImageFormatPNG) != E_POINTER ||
        image.Save(encoded.ptr, nullptr) != E_POINTER ||
        image.Save(encoded.ptr, &invalidType) != E_INVALIDARG)
        return fail(73, "Reject_invalid_IStream_save");
    STATSTG emptyStat{};
    if (FAILED(encoded.ptr->Stat(&emptyStat, STATFLAG_NONAME)) ||
        emptyStat.cbSize.QuadPart != 0)
        return fail(74, "Failed_save_did_not_write_to_stream");

    HRESULT hr = image.Save(encoded.ptr, &Gdiplus::ImageFormatPNG);
    if (FAILED(hr)) return fail(75, "IStream_Save_PNG");
    const BYTE pngSignature[8] = {137,80,78,71,13,10,26,10};
    if (!encoded.Peek(pngSignature, sizeof(pngSignature)))
        return fail(76, "IStream_PNG_signature");

    ATL::CImage restored;
    hr = restored.Load(encoded.ptr);
    if (FAILED(hr) || !restored.HasAlphaChannel() ||
        restored.GetWidth() != 2 || restored.GetHeight() != 2 ||
        restored.GetBPP() != 32)
        return fail(77, "IStream_Load_PNG");
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x) {
            const BYTE* actual = static_cast<const BYTE*>(
                restored.GetPixelAddress(x, y));
            if (!actual) return fail(78, "IStream_pixel_pointer");
            for (int channel = 0; channel < 4; ++channel)
                if (actual[channel] != colors[2 * y + x][channel])
                    return fail(79, "IStream_alpha_roundtrip");
        }
    if (restored.Load(encoded.ptr) != HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS))
        return fail(80, "IStream_refuse_replacement");

    // Releasing the caller-owned stream cannot invalidate the materialized
    // bitmap. GDI+ holds no outstanding reference on return.
    encoded.ptr->Release();
    encoded.ptr = nullptr;
    if (restored.GetPixelAddress(1, 1) == nullptr ||
        static_cast<const BYTE*>(restored.GetPixelAddress(1, 1))[3] != 200)
        return fail(81, "IStream_borrowed_lifetime");

    ATL::CImage rgb;
    if (!rgb.Create(3, 2, 24)) return fail(82, "IStream_BMP_source");
    rgb.SetPixel(0, 0, RGB(12, 34, 56));
    rgb.SetPixel(2, 1, RGB(123, 111, 99));
    OwnedStream bmp;
    if (!bmp.Create()) return fail(83, "IStream_BMP_create");
    hr = rgb.Save(bmp.ptr, &Gdiplus::ImageFormatBMP);
    if (FAILED(hr)) return fail(84, "IStream_Save_BMP");
    const BYTE bmpSignature[2] = {'B','M'};
    if (!bmp.Peek(bmpSignature, sizeof(bmpSignature)))
        return fail(85, "IStream_BMP_signature");
    ATL::CImage decoded;
    hr = decoded.Load(bmp.ptr);
    if (FAILED(hr) || decoded.GetWidth() != 3 || decoded.GetHeight() != 2 ||
        decoded.GetPixel(0, 0) != RGB(12,34,56) ||
        decoded.GetPixel(2, 1) != RGB(123,111,99))
        return fail(86, "IStream_BMP_roundtrip");

    // Malformed encoded bytes and unknown formats fail without partial
    // ownership transfer into CImage.
    OwnedStream corrupt;
    if (!corrupt.Create()) return fail(87, "IStream_corrupt_create");
    const BYTE badBytes[8] = {0,1,2,3,4,5,6,7};
    if (!corrupt.Put(badBytes, sizeof(badBytes)) || !corrupt.Rewind())
        return fail(88, "IStream_corrupt_write");
    ATL::CImage failed;
    hr = failed.Load(corrupt.ptr);
    if (SUCCEEDED(hr) || !failed.IsNull() || failed.GetBits() != nullptr)
        return fail(89, "IStream_corrupt_cleanup");
    if (failed.Load(static_cast<IStream*>(nullptr)) != E_POINTER ||
        !failed.IsNull())
        return fail(90, "IStream_null_rejection");

    // GDI+ can decode GIF, but this API explicitly admits only BMP/PNG.
    OwnedStream gif;
    if (!gif.Create()) return fail(91, "IStream_GIF_create");
    const BYTE sampleGif[] = {
        0x47,0x49,0x46,0x38,0x39,0x61,0x01,0x00,0x01,0x00,
        0x80,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,
        0x21,0xf9,0x04,0x01,0x00,0x00,0x00,0x00,
        0x2c,0x00,0x00,0x00,0x00,0x01,0x00,0x01,0x00,0x00,
        0x02,0x02,0x44,0x01,0x00,0x3b
    };
    if (!gif.Put(sampleGif, sizeof(sampleGif)) || !gif.Rewind())
        return fail(92, "IStream_GIF_write");
    hr = failed.Load(gif.ptr);
    if (hr != E_NOTIMPL || !failed.IsNull())
        return fail(93, "IStream_reject_GIF");

    return 0;
}


// An unregistered, stack-owned IStream test double. It has no CLSID,
// factory, class-object registration or COM Server capability. Its sole
// purpose is to force deterministic IStream::Write completion behaviors.
class FaultWriteStream final : public IStream {
public:
    enum Policy { ShortPositive, ShortZero, FailImmediately, FailSecond };
    explicit FaultWriteStream(Policy policy) noexcept : policy_(policy) {}
    FaultWriteStream(const FaultWriteStream&) = delete;
    FaultWriteStream& operator=(const FaultWriteStream&) = delete;

    ULONG calls = 0;
    ULONG otherCalls = 0;
    ULONGLONG acceptedBytes = 0;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) override {
        if (output == nullptr) return E_POINTER;
        *output = nullptr;
        if (::IsEqualIID(iid, IID_IUnknown) ||
            ::IsEqualIID(iid, IID_ISequentialStream) ||
            ::IsEqualIID(iid, IID_IStream)) {
            *output = static_cast<IStream*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        if (references_ > 0) --references_;
        return references_;
    }
    HRESULT STDMETHODCALLTYPE Read(void*, ULONG, ULONG* got) override {
        ++otherCalls;
        if (got != nullptr) *got = 0;
        return STG_E_ACCESSDENIED;
    }
    HRESULT STDMETHODCALLTYPE Write(const void* src, ULONG count,
                                    ULONG* wrote) override {
        ++calls;
        if (wrote != nullptr) *wrote = 0;
        if (src == nullptr || count == 0 || wrote == nullptr)
            return E_INVALIDARG;
        if (policy_ == FailImmediately ||
            (policy_ == FailSecond && calls >= 2))
            return STG_E_MEDIUMFULL;
        if (policy_ == ShortZero)
            return S_OK;
        if (policy_ == ShortPositive) {
            *wrote = count - 1;
            acceptedBytes += *wrote;
            return S_OK;
        }
        *wrote = count;
        acceptedBytes += count;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER, DWORD, ULARGE_INTEGER*) override {
        ++otherCalls; return STG_E_INVALIDFUNCTION;
    }
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override {
        ++otherCalls; return STG_E_INVALIDFUNCTION;
    }
    HRESULT STDMETHODCALLTYPE CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER*,
                                     ULARGE_INTEGER*) override {
        ++otherCalls; return STG_E_INVALIDFUNCTION;
    }
    HRESULT STDMETHODCALLTYPE Commit(DWORD) override {
        ++otherCalls; return STG_E_INVALIDFUNCTION;
    }
    HRESULT STDMETHODCALLTYPE Revert() override {
        ++otherCalls; return STG_E_INVALIDFUNCTION;
    }
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER,
                                          DWORD) override {
        ++otherCalls; return STG_E_INVALIDFUNCTION;
    }
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER,
                                            DWORD) override {
        ++otherCalls; return STG_E_INVALIDFUNCTION;
    }
    HRESULT STDMETHODCALLTYPE Stat(STATSTG*, DWORD) override {
        ++otherCalls; return STG_E_INVALIDFUNCTION;
    }
    HRESULT STDMETHODCALLTYPE Clone(IStream** clone) override {
        ++otherCalls;
        if (clone != nullptr) *clone = nullptr;
        return STG_E_INVALIDFUNCTION;
    }

private:
    Policy policy_;
    ULONG references_ = 1;  // Test-stack lifetime; no external references.
};

static int encoder_failure_injection() {
    ATL::CImage source;
    if (!source.Create(128, 96, 24))
        return fail(101, "write_failure_create_image");
    // Deterministic incompressible image: encoding spans multiple 8-KiB
    // transfer chunks, including a failure on the second IStream::Write.
    std::uint32_t random = 0x5b1d940fu;
    for (int y = 0; y < source.GetHeight(); ++y) {
        BYTE* row = static_cast<BYTE*>(source.GetPixelAddress(0, y));
        if (!row) return fail(102, "write_failure_pixel_row");
        for (int x = 0; x < source.GetWidth() * 3; ++x) {
            random = random * 1664525u + 1013904223u;
            row[x] = static_cast<BYTE>(random >> 24);
        }
    }

    FaultWriteStream shortWrite(FaultWriteStream::ShortPositive);
    HRESULT hr = source.Save(&shortWrite, &Gdiplus::ImageFormatPNG);
    if (hr != STG_E_WRITEFAULT || shortWrite.calls != 1 ||
        shortWrite.acceptedBytes == 0 || shortWrite.otherCalls != 0)
        return fail(103, "reject_S_OK_partial_write");

    FaultWriteStream zeroWrite(FaultWriteStream::ShortZero);
    hr = source.Save(&zeroWrite, &Gdiplus::ImageFormatPNG);
    if (hr != STG_E_WRITEFAULT || zeroWrite.calls != 1 ||
        zeroWrite.acceptedBytes != 0 || zeroWrite.otherCalls != 0)
        return fail(104, "reject_S_OK_zero_write");

    FaultWriteStream failed(FaultWriteStream::FailImmediately);
    hr = source.Save(&failed, &Gdiplus::ImageFormatPNG);
    if (hr != STG_E_MEDIUMFULL || failed.calls != 1 ||
        failed.acceptedBytes != 0 || failed.otherCalls != 0)
        return fail(105, "propagate_first_Write_HRESULT");

    FaultWriteStream midflight(FaultWriteStream::FailSecond);
    hr = source.Save(&midflight, &Gdiplus::ImageFormatPNG);
    if (hr != STG_E_MEDIUMFULL || midflight.calls != 2 ||
        midflight.acceptedBytes != 8192 || midflight.otherCalls != 0)
        return fail(106, "propagate_second_Write_HRESULT");

    // Even a failed sink must not change the caller's HBITMAP or leave a
    // GDI+ stream reference behind; a later ordinary Save must succeed.
    if (source.IsNull() || source.GetWidth() != 128)
        return fail(107, "preserve_image_on_failed_write");
    OwnedStream good;
    if (!good.Create()) return fail(108, "good_stream_after_failure");
    hr = source.Save(good.ptr, &Gdiplus::ImageFormatPNG);
    const BYTE png[8] = {137,80,78,71,13,10,26,10};
    if (FAILED(hr) || !good.Peek(png, sizeof(png)))
        return fail(109, "recover_from_earlier_stream_failures");
    return 0;
}


// Test-only, unregistered IStream that presents deterministic Read failures.
// No CLSID, registry, class factory, application-owned COM Server, or
// additional COM apartment. Every other operation is forbidden to verify
// the decoder snapshots only Read bytes from its borrowed source.
class FaultReadStream final : public IStream {
public:
    enum Policy { ShortPositive, ShortZero, FailImmediately, FailSecond,
                  EarlyEof, VerifiedEOF, OversizedKnown, ExactlyAtLimit };
    FaultReadStream(IStream* backing, Policy policy) noexcept
        : backing_(backing), policy_(policy) {}
    FaultReadStream(const FaultReadStream&) = delete;
    FaultReadStream& operator=(const FaultReadStream&) = delete;

    ULONG calls = 0;
    ULONG otherCalls = 0;
    ULONGLONG suppliedBytes = 0;
    ULONG refs = 1;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) override {
        ++otherCalls;
        if (output == nullptr) return E_POINTER;
        *output = nullptr;
        if (::IsEqualIID(iid, IID_IUnknown) ||
            ::IsEqualIID(iid, IID_IStream) ||
            ::IsEqualIID(iid, IID_ISequentialStream)) {
            *output = static_cast<IStream*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        ++otherCalls;
        return ++refs;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        ++otherCalls;
        return --refs;
    }
    HRESULT STDMETHODCALLTYPE Read(void* out, ULONG amount, ULONG* got) override {
        ++calls;
        if (got != nullptr) *got = 0;
        if (backing_ == nullptr || out == nullptr || amount == 0 ||
            got == nullptr)
            return E_INVALIDARG;
        if (policy_ == FailImmediately ||
            policy_ == OversizedKnown || policy_ == ExactlyAtLimit ||
            (policy_ == FailSecond && calls >= 2))
            return STG_E_MEDIUMFULL;
        if (policy_ == ShortZero)
            return S_OK;
        if (policy_ == VerifiedEOF) {
            const HRESULT inner = backing_->Read(out, amount, got);
            suppliedBytes += *got;
            // Emulate the Windows HGLOBAL behavior at logical EOF.
            return inner == S_FALSE ? S_OK : inner;
        }
        if (policy_ == ShortPositive) {
            const ULONG count = amount > 1 ? amount - 1 : 0;
            const HRESULT inner = backing_->Read(out, count, got);
            if (FAILED(inner)) return inner;
            suppliedBytes += *got;
            return S_OK;
        }
        if (policy_ == EarlyEof) {
            const HRESULT inner = backing_->Read(out, 1, got);
            if (FAILED(inner)) return inner;
            suppliedBytes += *got;
            return S_FALSE;
        }
        const HRESULT inner = backing_->Read(out, amount, got);
        suppliedBytes += *got;
        return inner;
    }
    HRESULT STDMETHODCALLTYPE Write(const void*, ULONG, ULONG* wrote) override {
        ++otherCalls;
        if (wrote) *wrote = 0;
        return STG_E_ACCESSDENIED;
    }
    HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER delta, DWORD origin,
                                   ULARGE_INTEGER* position) override {
        ++otherCalls;
        if (policy_ == VerifiedEOF)
            return backing_->Seek(delta, origin, position);
        if (policy_ == OversizedKnown || policy_ == ExactlyAtLimit) {
            if (origin != STREAM_SEEK_CUR || delta.QuadPart != 0)
                return STG_E_INVALIDFUNCTION;
            if (position != nullptr) position->QuadPart = 0;
            return S_OK;
        }
        return STG_E_INVALIDFUNCTION;
    }
    HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override {
        ++otherCalls; return STG_E_INVALIDFUNCTION;
    }
    HRESULT STDMETHODCALLTYPE CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER*,
                                     ULARGE_INTEGER*) override {
        ++otherCalls; return STG_E_INVALIDFUNCTION;
    }
    HRESULT STDMETHODCALLTYPE Commit(DWORD) override {
        ++otherCalls; return STG_E_INVALIDFUNCTION;
    }
    HRESULT STDMETHODCALLTYPE Revert() override {
        ++otherCalls; return STG_E_INVALIDFUNCTION;
    }
    HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER,
                                          DWORD) override {
        ++otherCalls; return STG_E_INVALIDFUNCTION;
    }
    HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER,
                                            DWORD) override {
        ++otherCalls; return STG_E_INVALIDFUNCTION;
    }
    HRESULT STDMETHODCALLTYPE Stat(STATSTG* out, DWORD flags) override {
        ++otherCalls;
        if (policy_ == VerifiedEOF)
            return backing_->Stat(out, flags);
        if (policy_ == OversizedKnown || policy_ == ExactlyAtLimit) {
            if (out == nullptr) return E_POINTER;
            *out = STATSTG{};
            out->type = STGTY_STREAM;
            out->cbSize.QuadPart = 64ULL * 1024ULL * 1024ULL +
                (policy_ == OversizedKnown ? 1ULL : 0ULL);
            return S_OK;
        }
        return STG_E_INVALIDFUNCTION;
    }
    HRESULT STDMETHODCALLTYPE Clone(IStream** clone) override {
        ++otherCalls;
        if (clone != nullptr) *clone = nullptr;
        return STG_E_INVALIDFUNCTION;
    }
private:
    IStream* backing_;
    Policy policy_;
};

static int decoder_failure_injection() {
    ATL::CImage input;
    if (!input.Create(128, 96, 24))
        return fail(111, "read_failure_create_image");
    std::uint32_t random = 0x5b1d940fu;
    for (int y = 0; y < input.GetHeight(); ++y) {
        BYTE* row = static_cast<BYTE*>(input.GetPixelAddress(0, y));
        if (row == nullptr) return fail(112, "read_failure_source_row");
        for (int x = 0; x < input.GetWidth() * 3; ++x) {
            random = random * 1664525u + 1013904223u;
            row[x] = static_cast<BYTE>(random >> 24);
        }
    }
    OwnedStream full;
    if (!full.Create()) return fail(113, "read_failure_create_stream");
    HRESULT hr = input.Save(full.ptr, &Gdiplus::ImageFormatPNG);
    if (FAILED(hr)) return fail(114, "read_failure_encode_source");
    STATSTG encoded{};
    if (FAILED(full.ptr->Stat(&encoded, STATFLAG_NONAME)) ||
        encoded.cbSize.QuadPart <= 8192)
        return fail(115, "read_failure_source_must_exceed_one_chunk");

    struct Scenario {
        FaultReadStream::Policy mode;
        HRESULT expected;
        ULONG calls;
        ULONG otherCalls; // A short S_OK probes Stat once to verify EOF.
    };
    const Scenario cases[] = {
        {FaultReadStream::ShortPositive, STG_E_READFAULT, 1, 2},
        {FaultReadStream::ShortZero, STG_E_READFAULT, 1, 2},
        {FaultReadStream::FailImmediately, STG_E_MEDIUMFULL, 1, 1},
        {FaultReadStream::FailSecond, STG_E_MEDIUMFULL, 2, 1}
    };
    for (const auto& test : cases) {
        if (!full.Rewind())
            return fail(116, "read_failure_rewind");
        FaultReadStream stream(full.ptr, test.mode);
        ATL::CImage rejected;
        hr = rejected.Load(&stream);
        if (hr != test.expected || stream.calls != test.calls ||
            stream.otherCalls != test.otherCalls || stream.refs != 1 ||
            !rejected.IsNull() || rejected.GetWidth() != 0)
            return fail(117, "read_failure_hardened_HRESULT_and_owner");
    }

    // A source that explicitly reports S_OK for its final short read is
    // valid only if Stat + a nonmoving Seek(CUR, 0) confirm the stream end.
    // Verify this branch without relying on an OS-version-specific stream.
    if (!full.Rewind())
        return fail(124, "read_failure_verified_eof_rewind");
    FaultReadStream confirmed(full.ptr, FaultReadStream::VerifiedEOF);
    ATL::CImage confirmedImage;
    hr = confirmedImage.Load(&confirmed);
    if (FAILED(hr) || confirmedImage.GetWidth() != 128 ||
        confirmedImage.GetHeight() != 96 ||
        confirmed.otherCalls != 4 || confirmed.refs != 1 ||
        confirmed.calls < 2)
        return fail(125, "read_failure_proven_short_S_OK_EOF");

    // A stat-able range greater than 64 MiB must be rejected without
    // making even one Read call or allocating a huge temporary stream.
    FaultReadStream tooLarge(full.ptr, FaultReadStream::OversizedKnown);
    ATL::CImage oversizedImage;
    hr = oversizedImage.Load(&tooLarge);
    if (hr != HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE) ||
        tooLarge.calls != 0 || tooLarge.otherCalls != 2 ||
        tooLarge.refs != 1 || !oversizedImage.IsNull())
        return fail(127, "preflight_64MiB_encoded_limit");

    // Exactly the maximum reported size is not itself invalid; actual
    // I/O failure must propagate and must not masquerade as too large.
    FaultReadStream atLimit(full.ptr, FaultReadStream::ExactlyAtLimit);
    ATL::CImage atLimitImage;
    hr = atLimitImage.Load(&atLimit);
    if (hr != STG_E_MEDIUMFULL || atLimit.calls != 1 ||
        atLimit.otherCalls != 2 || !atLimitImage.IsNull())
        return fail(128, "encoded_limit_boundary");

    if (!full.Rewind())
        return fail(118, "read_failure_early_eof_rewind");
    FaultReadStream early(full.ptr, FaultReadStream::EarlyEof);
    ATL::CImage truncated;
    hr = truncated.Load(&early);
    if (SUCCEEDED(hr) || early.calls != 1 ||
        early.otherCalls != 1 || early.refs != 1 ||
        !truncated.IsNull() || truncated.GetBits() != nullptr)
        return fail(119, "read_failure_truncated_input");

    OwnedStream empty;
    if (!empty.Create())
        return fail(120, "read_failure_empty_stream");
    ATL::CImage noInput;
    hr = noInput.Load(empty.ptr);
    if (hr != STG_E_READFAULT || !noInput.IsNull())
        return fail(121, "read_failure_empty_body");

    // Failure is isolated to each invocation. The caller's original
    // stream is still owned by the caller and can be repositioned.
    if (!full.Rewind())
        return fail(122, "read_failure_recover_rewind");
    ATL::CImage recovered;
    hr = recovered.Load(full.ptr);
    if (FAILED(hr) || recovered.GetWidth() != 128 ||
        recovered.GetHeight() != 96 || recovered.GetBits() == nullptr)
        return fail(123, "read_failure_success_after_faults");
    return 0;
}

int main() {
    static_assert(sizeof(void*) == 8, "Only Windows x64 and ARM64 qualified");
    ULONG_PTR token = 0;
    if (::CoGetContextToken(&token) != CO_E_NOTINITIALIZED)
        return fail(1, "Unexpected_initial_COM_apartment");
    if (int result = dib_and_draw()) return result;
    if (int result = bottom_up_attach()) return result;
    if (int result = codec_roundtrip()) return result;
    if (int result = stream_roundtrip()) return result;
    if (int result = encoder_failure_injection()) return result;
    if (int result = decoder_failure_injection()) return result;
    if (::CoGetContextToken(&token) != CO_E_NOTINITIALIZED)
        return fail(2, "COM_apartment_leaked");
    std::puts("ATL_IMAGE_OK dib=1 attach=1 draw=1 orientation=1 codec=1 stream=1 shortwrite=1 shortread=1 budget=1 alpha=1 cleanup=1 no_com=1");
    return 0;
}
