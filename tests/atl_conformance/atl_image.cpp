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

int main() {
    static_assert(sizeof(void*) == 8, "Only Windows x64 and ARM64 qualified");
    ULONG_PTR token = 0;
    if (::CoGetContextToken(&token) != CO_E_NOTINITIALIZED)
        return fail(1, "Unexpected_initial_COM_apartment");
    if (int result = dib_and_draw()) return result;
    if (int result = bottom_up_attach()) return result;
    if (::CoGetContextToken(&token) != CO_E_NOTINITIALIZED)
        return fail(2, "COM_apartment_leaked");
    std::puts("ATL_IMAGE_OK dib=1 attach=1 draw=1 orientation=1 no_com=1");
    return 0;
}
