// Real OLEAUT32 on-disk type-library acceptance, not a handwritten TLB.
//
// CreateTypeLib2 writes a genuine binary .tlb into a GUID-owned temporary
// file, then REGKIND_NONE reads it without registering a TypeLib globally.
// HRESULTs and member name resolution come from Windows ITypeInfo itself.
#include <atlcom.h>
#include <cstdio>
#include <string>
#include <oleauto.h>

#define FILE_TLB_CHECK(x) do { if (!(x)) { \
    std::fprintf(stderr, "ATL_FILE_TLB_FAIL line=%d expr=%s winerr=%lu\n", \
        __LINE__, #x, static_cast<unsigned long>(::GetLastError())); \
    return false; \
} } while (0)

namespace {
inline constexpr IID iidFile =
    {0xe14a9300, 0x51b8, 0x4afd, {0x9b,0x2b,0x2b,0xc2,0x03,0x87,0x50,0x71}};
inline constexpr GUID libFile =
    {0xe14a9301, 0x51b8, 0x4afd, {0x9b,0x2b,0x2b,0xc2,0x03,0x87,0x50,0x71}};
inline constexpr GUID libOther =
    {0xe14a9302, 0x51b8, 0x4afd, {0x9b,0x2b,0x2b,0xc2,0x03,0x87,0x50,0x71}};

class FileLibIdModule : public ATL::CAtlDllModuleT<FileLibIdModule> {
public:
    DECLARE_LIBID(libFile)
};

struct TempTypeLib {
    std::wstring path;
    bool CreatePath() {
        wchar_t directory[MAX_PATH+2]{};
        DWORD length = ::GetTempPathW(MAX_PATH, directory);
        if (!length || length >= MAX_PATH) return false;
        GUID randomId{};
        if (FAILED(::CoCreateGuid(&randomId))) return false;
        wchar_t guid[40]{};
        if (!::StringFromGUID2(randomId, guid, 40)) return false;
        path.assign(directory, length);
        path += L"OpenATL-TypeLib-";
        path += guid;
        path += L".tlb";
        return path.size() < MAX_PATH + 256;
    }
    ~TempTypeLib() {
        if (!path.empty()) (void)::DeleteFileW(path.c_str());
    }
};

// Create a real binary Automation TYPELIB, including a real DISPID/name;
// no registry calls, MIDL substitution or synthetic TypeInfo objects.
bool CreateDiskTypeLib(LPCOLESTR path) {
    ATL::CComPtr<ICreateTypeLib2> writer;
    FILE_TLB_CHECK(::CreateTypeLib2(SYS_WIN32, path, &writer.p) == S_OK &&
                   writer.p);
    FILE_TLB_CHECK(writer->SetGuid(libFile) == S_OK);
    FILE_TLB_CHECK(writer->SetVersion(1,0) == S_OK);
    FILE_TLB_CHECK(writer->SetName(const_cast<LPOLESTR>(L"OpenATLFileLib")) == S_OK);
    ATL::CComPtr<ICreateTypeInfo> type;
    FILE_TLB_CHECK(writer->CreateTypeInfo(
        const_cast<LPOLESTR>(L"IFileDual"), TKIND_DISPATCH,
        &type.p) == S_OK && type.p);
    FILE_TLB_CHECK(type->SetGuid(iidFile) == S_OK);
    FILE_TLB_CHECK(type->SetTypeFlags(TYPEFLAG_FDISPATCHABLE) == S_OK);
    FUNCDESC method{};
    method.memid = 77;
    method.funckind = FUNC_DISPATCH;
    method.invkind = INVOKE_FUNC;
    method.callconv = CC_STDCALL;
    method.cParams = 0;
    method.elemdescFunc.tdesc.vt = VT_I4;
    FILE_TLB_CHECK(type->AddFuncDesc(0, &method) == S_OK);
    LPOLESTR names[] = {const_cast<LPOLESTR>(L"ReadNumber")};
    FILE_TLB_CHECK(type->SetFuncAndParamNames(0, names, 1) == S_OK);
    FILE_TLB_CHECK(type->LayOut() == S_OK);
    type.Release();
    FILE_TLB_CHECK(writer->SaveAllChanges() == S_OK);
    writer.Release();
    return true;
}
bool TestFileTypeLib() {
    ATL::CComTypeInfoHolder holder;
    TempTypeLib file;
    FILE_TLB_CHECK(file.CreatePath());
    ITypeInfo* invalid = reinterpret_cast<ITypeInfo*>(1);
    FILE_TLB_CHECK(holder.GetTIFromFile(L"relative.tlb", &iidFile,
        &libFile, 1, 0, &invalid) == E_INVALIDARG && invalid == nullptr);
    FILE_TLB_CHECK(holder.GetTIFromFile(file.path, &iidFile,
        &libFile, 1, 0, nullptr) == E_POINTER);
    ATL::CComPtr<ITypeLib> registered;
    FILE_TLB_CHECK(FAILED(::LoadRegTypeLib(libFile,1,0,LOCALE_SYSTEM_DEFAULT,
        &registered.p)) && !registered.p);
    FILE_TLB_CHECK(CreateDiskTypeLib(file.path.c_str()));

    ATL::CComPtr<ITypeInfo> info;
    FILE_TLB_CHECK(holder.GetTIFromFile(file.path, &iidFile, &libFile, 1, 0,
        &info.p) == S_OK && info.p);
    TYPEATTR* attr = nullptr;
    FILE_TLB_CHECK(info->GetTypeAttr(&attr) == S_OK && attr);
    FILE_TLB_CHECK(::IsEqualGUID(attr->guid,iidFile) && attr->typekind == TKIND_DISPATCH);
    info->ReleaseTypeAttr(attr);
    LPOLESTR name[] = {const_cast<LPOLESTR>(L"ReadNumber")};
    MEMBERID id = MEMBERID_NIL;
    FILE_TLB_CHECK(info->GetIDsOfNames(name, 1, &id) == S_OK && id == 77);
    info.Release();
    // Windows accepts both absolute drive-path separator spellings.
    // CMake passes forward slashes for its generated COMMap.tlb path.
    std::wstring forwardSlashes = file.path;
    for (wchar_t& ch : forwardSlashes)
        if (ch == L'\\') ch = L'/';
    ATL::CComPtr<ITypeInfo> forwardSlashInfo;
    FILE_TLB_CHECK(holder.GetTIFromFile(forwardSlashes, &iidFile, &libFile,
        1, 0, &forwardSlashInfo.p) == S_OK && forwardSlashInfo.p);
    forwardSlashInfo.Release();

    invalid = reinterpret_cast<ITypeInfo*>(1);
    FILE_TLB_CHECK(holder.GetTIFromFile(file.path, &iidFile, &libOther,
        1, 0, &invalid) == TYPE_E_ELEMENTNOTFOUND && !invalid);
    invalid = reinterpret_cast<ITypeInfo*>(1);
    FILE_TLB_CHECK(holder.GetTIFromFile(file.path, &iidFile, &libFile,
        2, 0, &invalid) == TYPE_E_ELEMENTNOTFOUND && !invalid);
    invalid = reinterpret_cast<ITypeInfo*>(1);
    FILE_TLB_CHECK(holder.GetTIFromFile(file.path, &IID_IStream, &libFile,
        1, 0, &invalid) == TYPE_E_ELEMENTNOTFOUND && !invalid);
    invalid = reinterpret_cast<ITypeInfo*>(1);
    FILE_TLB_CHECK(holder.GetTIFromFile(file.path, &iidFile, &libFile,
        0xffff,0xffff, &invalid) == E_INVALIDARG && !invalid);
    FILE_TLB_CHECK(FAILED(::LoadRegTypeLib(libFile,1,0,LOCALE_SYSTEM_DEFAULT,
        &registered.p)) && !registered.p);
    // No implicit LoadTypeLibEx(... REGKIND_REGISTER) or HKCR writes.
    return true;
}
bool TestLibIdModule() {
    GUID current{};
    FILE_TLB_CHECK(ATL::_pAtlModule == nullptr);
    {
        FileLibIdModule module;
        FILE_TLB_CHECK(ATL::_pAtlModule == &module);
        FILE_TLB_CHECK(::IsEqualGUID(ATL::CAtlModule::m_libid,libFile));
        FILE_TLB_CHECK(FileLibIdModule::GetLibID(&current) == S_OK &&
                       ::IsEqualGUID(current,libFile));
        FILE_TLB_CHECK(FileLibIdModule::GetLibID(nullptr) == E_POINTER);
        FILE_TLB_CHECK(module.DllCanUnloadNow() == S_OK);
    }
    FILE_TLB_CHECK(ATL::_pAtlModule == nullptr);
    return true;
}
} // namespace

bool RunAtlFileTypeLibTests() {
    // The enclosing dispatch test already owns a real STA apartment.
    return TestLibIdModule() && TestFileTypeLib();
}
