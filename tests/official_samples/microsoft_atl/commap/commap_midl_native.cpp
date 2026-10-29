// Real pinned Microsoft COMMap.idl -> Windows SDK MIDL output acceptance.
//
// COMMap.h/COMMap_i.c/COMMap.tlb are generated from the byte-identical
// Microsoft IDL by the selected native Windows SDK midl.exe. No synthetic
// interfaces, Microsoft ATL headers, regsvr32 or typelib registry writes.
#include <atlcom.h>
#include "COMMap.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <oleauto.h>

#define ORIGINAL_MIDL_CHECK(expr) do { if (!(expr)) { \
    std::fprintf(stderr,"MICROSOFT_COMMAP_MIDL_FAIL line=%d expr=%s\n", \
        __LINE__, #expr); return false; } } while (0)

namespace {
class Apartment final {
public:
    Apartment() noexcept : hr_(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~Apartment() { if (SUCCEEDED(hr_)) ::CoUninitialize(); }
    HRESULT Status() const noexcept { return hr_; }
private:
    HRESULT hr_;
};
bool ToWide(const char* input, std::wstring& path) {
    if (!input || !*input) return false;
    const size_t bytes = std::strlen(input);
    if (bytes > 8192) return false;
    const int count = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        input, static_cast<int>(bytes), nullptr, 0);
    if (count <= 0) return false;
    path.resize(count);
    return ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        input, static_cast<int>(bytes), &path[0], count) == count;
}
bool ValidateOriginal(ITypeInfo* info, REFIID originalIID,
                      LPCOLESTR expectedName) {
    ORIGINAL_MIDL_CHECK(info && expectedName);
    TYPEATTR* attr = nullptr;
    ORIGINAL_MIDL_CHECK(info->GetTypeAttr(&attr) == S_OK && attr);
    const bool identical = ::IsEqualGUID(attr->guid, originalIID);
    info->ReleaseTypeAttr(attr);
    ORIGINAL_MIDL_CHECK(identical);
    LPOLESTR method[] = {const_cast<LPOLESTR>(expectedName)};
    MEMBERID id = MEMBERID_NIL;
    ORIGINAL_MIDL_CHECK(info->GetIDsOfNames(method, 1, &id) == S_OK &&
                        id != MEMBERID_NIL);
    return true;
}
} // namespace

bool RunOriginalCommapTypeLibTest(const char* file) {
    ULONG_PTR token = 0;
    ORIGINAL_MIDL_CHECK(::CoGetContextToken(&token) == CO_E_NOTINITIALIZED);
    {
        Apartment apartment;
        ORIGINAL_MIDL_CHECK(apartment.Status() == S_OK);
        std::wstring path;
        ORIGINAL_MIDL_CHECK(ToWide(file, path));
        ATL::CComPtr<ITypeLib> library;
        ORIGINAL_MIDL_CHECK(::LoadTypeLibEx(path.c_str(), REGKIND_NONE,
                                            &library.p) == S_OK && library.p);
        TLIBATTR* attrs = nullptr;
        ORIGINAL_MIDL_CHECK(library->GetLibAttr(&attrs) == S_OK && attrs);
        const bool originalLibrary = ::IsEqualGUID(attrs->guid, LIBID_COMMAPLib) &&
            attrs->wMajorVerNum == 1 && attrs->wMinorVerNum == 0;
        library->ReleaseTLibAttr(attrs);
        ORIGINAL_MIDL_CHECK(originalLibrary);
        ORIGINAL_MIDL_CHECK(library->GetTypeInfoCount() >= 4);
        ATL::CComTypeInfoHolder holder;
        ATL::CComPtr<ITypeInfo> chain, outer;
        ORIGINAL_MIDL_CHECK(holder.GetTIFromFile(path, &IID_IChain,
            &LIBID_COMMAPLib, 1, 0, &chain.p) == S_OK && chain.p);
        ORIGINAL_MIDL_CHECK(holder.GetTIFromFile(path, &IID_IOuter,
            &LIBID_COMMAPLib, 1, 0, &outer.p) == S_OK && outer.p);
        ORIGINAL_MIDL_CHECK(ValidateOriginal(chain.p, IID_IChain, L"Name2"));
        ORIGINAL_MIDL_CHECK(ValidateOriginal(outer.p, IID_IOuter, L"GetName"));
        chain.Release(); outer.Release();

        ITypeInfo* absent = reinterpret_cast<ITypeInfo*>(1);
        ORIGINAL_MIDL_CHECK(holder.GetTIFromFile(path, &IID_IStream,
            &LIBID_COMMAPLib, 1, 0, &absent) == TYPE_E_ELEMENTNOTFOUND &&
            absent == nullptr);
        absent = reinterpret_cast<ITypeInfo*>(1);
        ORIGINAL_MIDL_CHECK(holder.GetTIFromFile(path, &IID_IChain,
            &LIBID_COMMAPLib, 2, 0, &absent) == TYPE_E_ELEMENTNOTFOUND &&
            absent == nullptr);
    }
    ORIGINAL_MIDL_CHECK(::CoGetContextToken(&token) == CO_E_NOTINITIALIZED);
    return true;
}
