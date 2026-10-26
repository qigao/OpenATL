/* Win32 ATL utility acceptance using OpenATL's OWN headers.
 * CAtlFile, CString and Win32 synchronization are independently authored/
 * MIT-derived code; no RATL, COM server, RGS or COM apartment required.
 */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <objbase.h>
#include <tchar.h>
#include <cstdio>
#include <cstring>
#include <utility>
#include <atlfile.h>
#include <atlsync.h>
#include <atlstr.h>

static int failure(int code, const char* stage) {
    std::fprintf(stderr, "ATL_CORE_FAIL code=%d stage=%s winerr=%lu\n",
                  code,stage,static_cast<unsigned long>(::GetLastError()));
    return code;
}
static bool no_com() {
    ULONG_PTR token=0;
    return ::CoGetContextToken(&token)==CO_E_NOTINITIALIZED;
}
static int strings() {
    ATL::CString value(_T("  alpha   "));
    if(value.Trim().Compare(_T("alpha"))!=0)return failure(10,"Trim");
    if(value.Right(100).Compare(_T("alpha"))!=0 ||
       value.Mid(999).GetLength()!=0 ||
       value.Mid(-4,2).Compare(_T("al"))!=0 ||
       !value.Left(-5).IsEmpty() ||
       !value.Right(-5).IsEmpty())
        return failure(11,"bounded_substring");

    ATL::CString formatted;
    formatted.Format(_T("file-%d-%s"),42,_T("ok"));
    if(formatted.Compare(_T("file-42-ok"))!=0)
        return failure(12,"Format_bounds");
    if(formatted.Replace(_T(""),_T("no"))!=0)
        return failure(13,"empty_replace");
    if(formatted.Remove(_T('-'))!=2 ||
       formatted.Compare(_T("file42ok"))!=0)
        return failure(14,"Remove");
    formatted.Append(_T("ZZZ"),2);
    if(formatted.Compare(_T("file42okZZ"))!=0)
        return failure(15,"Append");
    formatted.Truncate(4);
    if(formatted.Compare(_T("file"))!=0)
        return failure(16,"Truncate");
    formatted.Preallocate(32);
    TCHAR* buffer=formatted.GetBufferSetLength(3);
    if(!buffer)return failure(17,"GetBufferSetLength");
    buffer[0]=_T('A');buffer[1]=_T('B');buffer[2]=_T('C');
    formatted.ReleaseBuffer(3);
    if(formatted.Compare(_T("ABC"))!=0 ||
       formatted.Insert(500,_T('D'))!=4 ||
       formatted.Compare(_T("ABCD"))!=0 ||
       formatted.Delete(1,2)!=2 ||
       formatted.Compare(_T("AD"))!=0)
        return failure(18,"Buffer_insert_delete");
    ATL::CString empty;
    if(empty.GetBuffer()==nullptr || !empty.IsEmpty())
        return failure(19,"empty_GetBuffer");
#ifdef UNICODE
    ATL::CString converted("ASCII-OK");
    if(converted.Compare(L"ASCII-OK")!=0)
        return failure(20,"ANSI_to_Unicode_NUL");
#endif
    return 0;
}
struct TemporaryFile {
    wchar_t path[MAX_PATH] = {};
    bool create() {
        wchar_t directory[MAX_PATH]={};
        DWORD size=::GetTempPathW(MAX_PATH,directory);
        return size>0 && size<MAX_PATH &&
               ::GetTempFileNameW(directory,L"ATL",0,path)!=0;
    }
    ~TemporaryFile() {
        if(path[0])::DeleteFileW(path);
    }
};
static int files() {
    TemporaryFile name;
    if(!name.create())return failure(30,"temp_name");
    ATL::CAtlFile file;
    if(file.Read(nullptr,1)!=E_HANDLE ||
       file.Create(name.path,GENERIC_READ|GENERIC_WRITE,0,CREATE_ALWAYS)!=S_OK ||
       !file.IsOpen())
        return failure(31,"open_or_invalid_handle");
    const char data[]="data-stream";
    DWORD sent=0;
    if(file.Write(data,static_cast<DWORD>(sizeof(data)-1),&sent)!=S_OK ||
       sent!=sizeof(data)-1 || file.Flush()!=S_OK)
        return failure(32,"write_flush");
    ULONGLONG size=0;
    if(file.GetSize(&size)!=S_OK || size!=sizeof(data)-1 ||
       file.GetSize(nullptr)!=E_POINTER ||
       file.Seek(0,FILE_BEGIN)!=S_OK)
        return failure(33,"file_size_seek");
    char actual[32]={};
    DWORD received=0;
    if(file.Read(actual,static_cast<DWORD>(sizeof(data)-1),&received)!=S_OK ||
       received!=sizeof(data)-1 ||
       std::memcmp(actual,data,sizeof(data)-1)!=0)
        return failure(34,"file_read");
    if(file.Read(nullptr,1)!=E_POINTER ||
       file.Write(nullptr,1)!=E_POINTER ||
       file.SetSize(4)!=S_OK ||
       file.GetSize(&size)!=S_OK || size!=4)
        return failure(35,"invalid_buffers_and_truncate");
    ATL::CAtlFile moved(std::move(file));
    if(file.IsOpen() || !moved.IsOpen() || moved.Close()!=S_OK ||
       moved.IsOpen() || moved.Close()!=S_OK)
        return failure(36,"move_and_idempotent_close");
    return 0;
}
static int synchronization() {
    ATL::CEvent gate;
    if(gate.Wait(0)!=WAIT_FAILED ||
       !gate.Create(nullptr,TRUE,FALSE) ||
       gate.Wait(0)!=WAIT_TIMEOUT ||
       !gate.Set() ||
       gate.Wait(0)!=WAIT_OBJECT_0 ||
       !gate.Reset() ||
       gate.Wait(0)!=WAIT_TIMEOUT)
        return failure(40,"event");
    ATL::CEvent once;
    if(!once.Create(nullptr,FALSE,TRUE) ||
       once.Wait(0)!=WAIT_OBJECT_0 ||
       once.Wait(0)!=WAIT_TIMEOUT)
        return failure(41,"auto_reset");

    ATL::CMutex mutex;
    if(!mutex.Create() || mutex.Wait(0)!=WAIT_OBJECT_0 ||
       !mutex.Release())
        return failure(42,"mutex");
    ATL::CSemaphore semaphore;
    if(!semaphore.Create(nullptr,0,2) ||
       semaphore.Wait(0)!=WAIT_TIMEOUT ||
       !semaphore.Release(1) ||
       semaphore.Wait(0)!=WAIT_OBJECT_0 ||
       semaphore.Wait(0)!=WAIT_TIMEOUT)
        return failure(43,"semaphore");

    ATL::CCriticalSection section;
    if(section.Lock()!=FALSE || section.Init()!=S_OK ||
       section.Init()!=S_FALSE)
        return failure(44,"critical_init");
    if(!section.Lock() || !section.TryLock())
        return failure(45,"critical_recursive_lock");
    section.Unlock();
    section.Unlock();
    {
        ATL::CScopedCriticalSectionLock lock(section);
        if(!lock.IsLocked())return failure(46,"scoped_lock");
    }
    return 0;
}
int main() {
    if(!no_com())return failure(1,"COM_apartment_unexpected");
    int result=strings();
    if(!result)result=files();
    if(!result)result=synchronization();
    if(result)return result;
    if(!no_com())return failure(2,"ATL_utility_initialized_COM");
    std::puts("ATL_CORE_OK file=1 string=1 sync=1 no_com=1");
    return 0;
}
