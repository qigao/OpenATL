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
#include <atlbase.h>
#include <atlfile.h>
#include <atlsync.h>
#include <atlstr.h>

#if !defined(OPENATL_REPOSITORY_HEADERS) || OPENATL_REPOSITORY_HEADERS != 0x4F41544CUL
#error "This target must compile repository-owned OpenATL headers"
#endif

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
// Registry acceptance uses a process-unique HKCU leaf and NEVER deletes an
// existing key. Only an explicitly newly-created key belongs to this test.
struct TemporaryRegistry {
    TCHAR path[160] = {};
    bool owned = false;
    TemporaryRegistry() {
        LARGE_INTEGER counter = {};
        if (!::QueryPerformanceCounter(&counter))
            return;
        _stprintf_s(path, _countof(path), _T("Software\\OpenATL_RegKey_%lu_%08lx_%08lx"),
                    static_cast<unsigned long>(::GetCurrentProcessId()),
                    static_cast<unsigned long>(counter.HighPart),
                    static_cast<unsigned long>(counter.LowPart));
    }
    void MarkCreated(DWORD disposition) {
        owned = (disposition == REG_CREATED_NEW_KEY);
    }
    LSTATUS Cleanup() {
        if (!owned)
            return ERROR_INVALID_PARAMETER;
        LSTATUS status = ::RegDeleteTree(HKEY_CURRENT_USER, path);
        if (status == ERROR_SUCCESS)
            owned = false;
        return status;
    }
    ~TemporaryRegistry() {
        if (owned)
            (void)::RegDeleteTree(HKEY_CURRENT_USER, path);
    }
};

static int registry() {
    TemporaryRegistry temp;
    if (temp.path[0] == 0) return failure(50, "registry_fixture");
    ATL::CRegKey key;
    DWORD created = 0;
    const LSTATUS createStatus = key.Create(
        HKEY_CURRENT_USER, temp.path, nullptr, REG_OPTION_NON_VOLATILE,
        KEY_READ | KEY_WRITE, nullptr, &created);
    if (createStatus == ERROR_SUCCESS)
        temp.MarkCreated(created);
    if (createStatus != ERROR_SUCCESS || !key.m_hKey ||
        created != REG_CREATED_NEW_KEY)
        return failure(51, "registry_create");

    DWORD count = 77;
    if (key.SetDWORDValue(_T("Count"), 1234) != ERROR_SUCCESS ||
        key.QueryDWORDValue(_T("Count"), count) != ERROR_SUCCESS || count != 1234)
        return failure(52, "registry_dword");

    const TCHAR* label = _T("alpha-\u6771\u4eac");
    if (key.SetStringValue(_T("Label"), label) != ERROR_SUCCESS)
        return failure(53, "registry_string_write");
    ULONG chars = 0;
    if (key.QueryStringValue(_T("Label"), nullptr, &chars) != ERROR_SUCCESS ||
        chars < _tcslen(label) + 1 || chars > 64)
        return failure(54, "registry_string_size");
    TCHAR shortBuffer[2] = {};
    ULONG capacity = 2;
    if (key.QueryStringValue(_T("Label"), shortBuffer, &capacity) != ERROR_MORE_DATA ||
        capacity < _tcslen(label) + 1)
        return failure(55, "registry_small_buffer");
    TCHAR text[64] = {};
    capacity = _countof(text);
    if (key.QueryStringValue(_T("Label"), text, &capacity) != ERROR_SUCCESS ||
        _tcscmp(text, label) != 0 ||
        capacity != _tcslen(label) + 1)
        return failure(56, "registry_string_roundtrip");


    // The registry may store REG_SZ without its terminating NUL. Its
    // reported size must include a terminator, even for a size-only query.
    const TCHAR unterminated[] = {_T('r'), _T('a'), _T('w')};
    if (::RegSetValueEx(key.m_hKey, _T("NonTerminated"), 0, REG_SZ,
                        reinterpret_cast<const BYTE*>(unterminated),
                        sizeof(unterminated)) != ERROR_SUCCESS)
        return failure(67, "registry_nonterminated_fixture");
    ULONG malformedSize = 0;
    if (key.QueryStringValue(_T("NonTerminated"), nullptr, &malformedSize) !=
            ERROR_SUCCESS || malformedSize != _countof(unterminated) + 1)
        return failure(68, "registry_nonterminated_size");

    TCHAR noTermBuffer[3] = {_T('!'), _T('!'), _T('!')};
    ULONG exactCapacity = _countof(noTermBuffer);
    if (key.QueryStringValue(_T("NonTerminated"), noTermBuffer,
                             &exactCapacity) != ERROR_MORE_DATA ||
        exactCapacity != _countof(unterminated) + 1 ||
        noTermBuffer[0] != 0)
        return failure(69, "registry_nonterminated_no_partial");
    TCHAR terminated[4] = {};
    ULONG termCapacity = _countof(terminated);
    if (key.QueryStringValue(_T("NonTerminated"), terminated,
                             &termCapacity) != ERROR_SUCCESS ||
        termCapacity != _countof(terminated) ||
        _tcscmp(terminated, _T("raw")) != 0)
        return failure(70, "registry_nonterminated_repaired");

    // The expand-string API must not silently expand environment variables.
    const TCHAR* expanded = _T("%SystemRoot%\\suffix");
    if (key.SetStringValue(_T("Expandable"), expanded, REG_EXPAND_SZ) !=
            ERROR_SUCCESS)
        return failure(71, "registry_expand_fixture");
    TCHAR expansion[64] = {};
    ULONG expansionCapacity = _countof(expansion);
    if (key.QueryStringValue(_T("Expandable"), expansion,
                             &expansionCapacity) != ERROR_SUCCESS ||
        _tcscmp(expansion, expanded) != 0)
        return failure(72, "registry_expand_noexpand");

    // Incorrect types, missing value and null length are rejected, not
    // mistaken for a successfully initialized string.
    ULONG invalidCapacity = 4;
    TCHAR invalid[4] = {_T('!'), _T('!'), _T('!'), 0};
    if (key.QueryStringValue(_T("Count"), invalid,
                             &invalidCapacity) != ERROR_INVALID_DATATYPE ||
        key.QueryStringValue(_T("Missing"), invalid,
                             &invalidCapacity) != ERROR_FILE_NOT_FOUND ||
        key.QueryStringValue(_T("Label"), invalid, nullptr) !=
            ERROR_INVALID_PARAMETER)
        return failure(73, "registry_string_rejections");

    const BYTE source[] = {0, 1, 127, 255, 42};
    if (key.SetBinaryValue(_T("Raw"), source, sizeof(source)) != ERROR_SUCCESS)
        return failure(57, "registry_binary_write");
    ULONG bytes = 0;
    if (key.QueryBinaryValue(_T("Raw"), nullptr, &bytes) != ERROR_SUCCESS ||
        bytes != sizeof(source))
        return failure(58, "registry_binary_size");
    BYTE raw[sizeof(source)] = {};
    bytes = sizeof(raw);
    if (key.QueryBinaryValue(_T("Raw"), raw, &bytes) != ERROR_SUCCESS ||
        bytes != sizeof(source) || std::memcmp(raw, source, sizeof(raw)) != 0)
        return failure(59, "registry_binary_roundtrip");

    DWORD unchanged = 912;
    if (key.QueryDWORDValue(_T("Label"), unchanged) != ERROR_INVALID_DATATYPE ||
        unchanged != 912 ||
        key.Open(HKEY_CURRENT_USER, _T("Software\\OpenATL_nonexistent_sentinel"),
                 KEY_READ) != ERROR_FILE_NOT_FOUND ||
        !key.m_hKey ||
        key.SetBinaryValue(_T("Invalid"), nullptr, 1) != ERROR_INVALID_PARAMETER)
        return failure(60, "registry_errors_preserve_owner");

    HKEY transferred = key.Detach();
    if (transferred == nullptr || key.m_hKey != nullptr ||
        key.QueryDWORDValue(_T("Count"), count) != ERROR_INVALID_HANDLE)
        return failure(61, "registry_detach");
    ATL::CRegKey moved;
    if (moved.Attach(transferred) != ERROR_SUCCESS ||
        moved.m_hKey != transferred)
        return failure(62, "registry_attach");
    ATL::CRegKey movedAgain(std::move(moved));
    if (moved.m_hKey != nullptr ||
        movedAgain.QueryDWORDValue(_T("Count"), count) != ERROR_SUCCESS ||
        count != 1234)
        return failure(63, "registry_move");
    ATL::CRegKey readOnly;
    if (readOnly.Open(HKEY_CURRENT_USER, temp.path, KEY_READ) != ERROR_SUCCESS ||
        readOnly.SetDWORDValue(_T("Denied"), 1) != ERROR_ACCESS_DENIED)
        return failure(64, "registry_access_rights");

    if (movedAgain.DeleteValue(_T("Count")) != ERROR_SUCCESS ||
        movedAgain.QueryDWORDValue(_T("Count"), count) != ERROR_FILE_NOT_FOUND ||
        movedAgain.Close() != ERROR_SUCCESS ||
        movedAgain.Close() != ERROR_SUCCESS ||
        readOnly.Close() != ERROR_SUCCESS)
        return failure(65, "registry_delete_close");
    if (!no_com()) return failure(66, "registry_initialized_com");
    if (temp.Cleanup() != ERROR_SUCCESS)
        return failure(74, "registry_cleanup");
    ATL::CRegKey missingAfterCleanup;
    if (missingAfterCleanup.Open(HKEY_CURRENT_USER, temp.path, KEY_READ) !=
            ERROR_FILE_NOT_FOUND)
        return failure(75, "registry_cleanup_verified");
    return 0;
}

int main() {
    if(!no_com())return failure(1,"COM_apartment_unexpected");
    int result=strings();
    if(!result)result=files();
    if(!result)result=synchronization();
    if(!result)result=registry();
    if(result)return result;
    if(!no_com())return failure(2,"ATL_utility_initialized_COM");
    std::puts("ATL_CORE_OK file=1 string=1 sync=1 registry=1 no_com=1");
    return 0;
}
