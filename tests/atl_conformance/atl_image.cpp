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
    hr = rgb.Save(paths.bmp);
    if (FAILED(hr)) return fail(54, "bmp_save_from_extension");
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

int main() {
    static_assert(sizeof(void*) == 8, "Only Windows x64 and ARM64 qualified");
    ULONG_PTR token = 0;
    if (::CoGetContextToken(&token) != CO_E_NOTINITIALIZED)
        return fail(1, "Unexpected_initial_COM_apartment");
    if (int result = dib_and_draw()) return result;
    if (int result = bottom_up_attach()) return result;
    if (int result = codec_roundtrip()) return result;
    if (int result = stream_roundtrip()) return result;
    if (::CoGetContextToken(&token) != CO_E_NOTINITIALIZED)
        return fail(2, "COM_apartment_leaked");
    std::puts("ATL_IMAGE_OK dib=1 attach=1 draw=1 orientation=1 codec=1 stream=1 alpha=1 cleanup=1 no_com=1");
    return 0;
}
