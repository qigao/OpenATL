/* Native Win32 path acceptance. The library implementation is from the
 * current checkout, not the installed Microsoft ATL or a downloaded OpenATL.
 * No COM library, apartment, registry or external path library is needed.
 */
#include <windows.h>
#include <tchar.h>
#include <cstdio>
#include <string>
#include <atlpath.h>
#include <atlfile.h>
#include <atldef.h>

#ifndef OPENATL_REPOSITORY_HEADERS
#error "Path tests must compile repository-owned ATL headers"
#elif OPENATL_REPOSITORY_HEADERS != 0x4F41544CUL
#error "Unexpected ATL header implementation"
#endif

static int fail(int code, const char* stage) {
    std::fprintf(stderr,"ATL_PATH_FAIL code=%d stage=%s winerr=%lu\n",
                 code,stage,static_cast<unsigned long>(::GetLastError()));
    return code;
}

struct TestPaths {
    TCHAR seed[MAX_PATH] = {};
    std::basic_string<TCHAR> filename;
    bool Init() {
        TCHAR temp[MAX_PATH] = {};
        DWORD count=::GetTempPath(MAX_PATH,temp);
        if(!count || count>=MAX_PATH) return false;
        if(!::GetTempFileName(temp,_T("OAP"),0,seed)) return false;
        filename.assign(seed);
        // ASCII source and a genuine UTF-16 filename in the NT filesystem.
        filename += _T("_\\u6D4B\\u8BD5.log");
        return true;
    }
    ~TestPaths() {
        if(!filename.empty()) ::DeleteFile(filename.c_str());
        if(seed[0]) ::DeleteFile(seed);
    }
};

int main() {
    TestPaths tmp;
    if(!tmp.Init()) return fail(10,"GetTempFileName");
    ATL::CPath path(tmp.filename.c_str());
    if(path.IsEmpty() || path.IsRelative() ||
       _tcscmp(path.GetExtension(),_T(".log"))!=0 ||
       !_tcsstr(path.GetFileName(),_T("_\\u6D4B\\u8BD5.log")))
        return fail(11,"unicode_name_and_extension");

    // Round trip a real OS file through the path helper and local CAtlFile.
    ATL::CAtlFile file;
    if(file.Create(path.GetString(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,
                   CREATE_NEW)!=S_OK || !file.IsOpen() || !path.FileExists() ||
       path.IsDirectory())
        return fail(12,"unicode_CreateFile");
    const char payload[]="OpenATL";
    DWORD written=0;
    if(file.Write(payload,sizeof(payload)-1,&written)!=S_OK ||
       written!=sizeof(payload)-1 || file.Flush()!=S_OK ||
       file.Seek(0,FILE_BEGIN)!=S_OK)
        return fail(13,"Write_Seek_Flush");
    char got[sizeof(payload)]={};
    DWORD received=0;
    if(file.Read(got,sizeof(payload)-1,&received)!=S_OK ||
       received!=sizeof(payload)-1 ||
       ::memcmp(got,payload,sizeof(payload)-1)!=0 ||
       file.Close()!=S_OK)
        return fail(14,"Read_Close");

    ATL::CPath parent(path.GetString());
    if(!parent.RemoveFileSpec() || !parent.IsDirectory())
        return fail(15,"RemoveFileSpec_parent");
    ATL::CPath rebuilt(parent.GetString());
    if(!rebuilt.Append(path.GetFileName()) ||
       _tcscmp(rebuilt.GetString(),path.GetString())!=0)
        return fail(16,"Append_relative");
    if(rebuilt.Append(_T("C:\\\\escape.log")) ||
       rebuilt.Append(_T("\\\\server\\\\share")) ||
       _tcscmp(rebuilt.GetString(),path.GetString())!=0)
        return fail(17,"Reject_rooted_component");

    ATL::CPath shortened(path.GetString());
    if(!shortened.RemoveExtension() ||
       shortened.GetExtension()[0]!=_T('\\0') ||
       _tcscmp(shortened.GetFileName(),path.GetFileName())==0)
        return fail(18,"RemoveExtension");
    ATL::CPath drive(_T("C:\\\\file.log"));
    if(!drive.RemoveFileSpec() ||
       _tcscmp(drive.GetString(),_T("C:\\\\"))!=0 ||
       drive.IsRelative() ||
       !drive.AddBackslash())
        return fail(19,"Preserve_drive_root");
    ATL::CPath relative(_T("folder"));
    if(!relative.IsRelative() || !relative.Append(_T("leaf")) ||
       _tcscmp(relative.GetString(),_T("folder\\\\leaf"))!=0)
        return fail(20,"relative_append");

    if(!::DeleteFile(path.GetString()) || path.FileExists())
        return fail(21,"Close_releases_file_and_delete");
    std::puts("ATL_PATH_OK unicode=1 file=1 relative=1 root=1");
    return 0;
}
