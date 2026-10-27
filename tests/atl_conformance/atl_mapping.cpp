// OpenATL Win32 file mapping acceptance: real file, unaligned view,
// duplicate independent ownership, pagefile-backed named shared memory,
// failure cleanup, and no implicitly created COM apartment.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <ole2.h>
#include <atlfile.h>
#include <cstdio>
#include <cwchar>
#include <vector>
#include <cstdint>
#include <utility>

#if !defined(OPENATL_REPOSITORY_HEADERS) || OPENATL_REPOSITORY_HEADERS != 0x4F41544CUL
#error "The file mapping conformance requires in-tree OpenATL"
#endif

static int fail(int code, const char* phase, HRESULT hr = E_FAIL) {
    std::fprintf(stderr, "ATL_MAPPING_FAIL code=%d phase=%s hr=0x%08lx\n",
        code, phase, static_cast<unsigned long>(hr));
    return code;
}

struct DeleteTempFile {
    wchar_t path[MAX_PATH]{};
    ~DeleteTempFile() { if (path[0]) (void)::DeleteFileW(path); }
};

static int file_map() {
    SYSTEM_INFO info{};
    ::GetSystemInfo(&info);
    if (info.dwAllocationGranularity == 0)
        return fail(10, "allocation_granularity");

    wchar_t tmpDir[MAX_PATH]{};
    DWORD cch = ::GetTempPathW(MAX_PATH, tmpDir);
    if (cch == 0 || cch >= MAX_PATH)
        return fail(11, "GetTempPathW");
    DeleteTempFile cleanup;
    if (!::GetTempFileNameW(tmpDir, L"OAM", 0, cleanup.path))
        return fail(12, "GetTempFileNameW");

    ATL::CAtlFile file;
    HRESULT hr = file.Create(cleanup.path, GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ, CREATE_ALWAYS);
    if (FAILED(hr)) return fail(13, "CAtlFile_Create", hr);

    const size_t bytes = static_cast<size_t>(info.dwAllocationGranularity) * 2u + 4096u;
    std::vector<BYTE> expected(bytes);
    for (size_t i = 0; i < bytes; ++i)
        expected[i] = static_cast<BYTE>((i * 17u + 13u) & 0xffu);
    DWORD written = 0;
    hr = file.Write(expected.data(), static_cast<DWORD>(bytes), &written);
    if (FAILED(hr) || written != bytes)
        return fail(14, "CAtlFile_Write", hr);

    const ULONGLONG offset = static_cast<ULONGLONG>(info.dwAllocationGranularity) + 3ULL;
    ATL::CAtlFileMapping<BYTE> map;
    hr = map.MapFile(file.m_h, 37, offset, PAGE_READWRITE, FILE_MAP_WRITE);
    if (FAILED(hr) || !map.GetHandle() || !map.GetData() ||
        map.GetMappingSize() != 37)
        return fail(15, "MapFile_non_aligned", hr);

    BYTE* view = map;
    if (view[0] != expected[static_cast<size_t>(offset)] ||
        view[36] != expected[static_cast<size_t>(offset) + 36])
        return fail(16, "MapFile_view_offset");
    view[0] = 0xA5;
    view[36] = 0x5A;
    expected[static_cast<size_t>(offset)] = 0xA5;
    expected[static_cast<size_t>(offset) + 36] = 0x5A;

    ATL::CAtlFileMapping<BYTE> copy;
    hr = copy.CopyFrom(map);
    if (FAILED(hr) || copy.GetHandle() == map.GetHandle() ||
        copy.GetData() == map.GetData() ||
        copy.GetMappingSize() != map.GetMappingSize())
        return fail(17, "CopyFrom_independent_handle_view", hr);
    if (static_cast<BYTE*>(copy.GetData())[0] != 0xA5)
        return fail(18, "CopyFrom_shared_bytes");
    if (SUCCEEDED(copy.CopyFrom(map)) || SUCCEEDED(map.MapFile(file.m_h, 4)))
        return fail(19, "Reject_double_ownership");

    hr = map.Flush();
    if (FAILED(hr)) return fail(20, "FlushViewOfFile", hr);
    hr = map.Unmap();
    if (FAILED(hr) || map.GetHandle() || map.GetData() ||
        map.GetMappingSize() != 0)
        return fail(21, "Unmap_reset", hr);

    // The copy has its own section HANDLE and must outlive the original.
    if (static_cast<BYTE*>(copy.GetData())[36] != 0x5A)
        return fail(22, "Duplicate_survives_unmap");
    hr = copy.Unmap();
    if (FAILED(hr)) return fail(23, "Copy_Unmap", hr);

    ULONGLONG newPos = 0;
    hr = file.Seek(0, FILE_BEGIN, &newPos);
    if (FAILED(hr) || newPos != 0)
        return fail(24, "Seek_file");
    std::vector<BYTE> actual(bytes);
    DWORD read = 0;
    hr = file.Read(actual.data(), static_cast<DWORD>(bytes), &read);
    if (FAILED(hr) || read != bytes || actual != expected)
        return fail(25, "Mapping_flush_persistent_file_bytes", hr);

    if (SUCCEEDED(map.MapFile(file.m_h, 1, static_cast<ULONGLONG>(bytes))) ||
        SUCCEEDED(map.MapFile(file.m_h, 2, static_cast<ULONGLONG>(bytes-1))) ||
        map.GetHandle() || map.GetData())
        return fail(26, "Reject_file_mapping_oob");
    if (SUCCEEDED(map.CopyFrom(copy)))
        return fail(27, "Reject_unmapped_copy");

    // A zero length maps from offset to EOF, bounded to SIZE_T.
    hr = map.MapFile(file.m_h, 0, static_cast<ULONGLONG>(bytes-64));
    if (FAILED(hr) || map.GetMappingSize() != 64)
        return fail(28, "MapFile_to_EOF", hr);
    ATL::CAtlFileMapping<BYTE> moved(std::move(map));
    if (map.GetData() || map.GetHandle() ||
        !moved.GetData() || moved.GetMappingSize() != 64)
        return fail(29, "Mapping_move_ownership");
    if (FAILED(moved.Unmap())) return fail(30, "Unmap_moved");

    return 0;
}

static int named_shared_memory() {
    wchar_t name[128]{};
    const int n = std::swprintf(name, sizeof(name)/sizeof(name[0]),
        L"Local\\OpenATL-Mapping-%lu-%llu",
        static_cast<unsigned long>(::GetCurrentProcessId()),
        static_cast<unsigned long long>(::GetTickCount64()));
    if (n < 0 || static_cast<size_t>(n) >= sizeof(name)/sizeof(name[0]))
        return fail(40, "SharedMemory_name");

    ATL::CAtlFileMapping<BYTE> owner;
    BOOL exists = TRUE;
    HRESULT hr = owner.MapSharedMem(4096, name, &exists);
    if (FAILED(hr) || exists || !owner.GetData() ||
        owner.GetMappingSize() != 4096)
        return fail(41, "Create_named_shared_memory", hr);
    BYTE* bytes = owner;
    bytes[17] = 0x6E;

    ATL::CAtlFileMapping<BYTE> reopened;
    hr = reopened.OpenMapping(name, 1, 17, FILE_MAP_READ);
    if (FAILED(hr) || !reopened.GetData() ||
        static_cast<BYTE*>(reopened.GetData())[0] != 0x6E)
        return fail(42, "OpenMapping_unaligned_offset", hr);

    ATL::CAtlFileMapping<BYTE> second;
    exists = FALSE;
    hr = second.MapSharedMem(4096, name, &exists);
    if (FAILED(hr) || !exists ||
        static_cast<BYTE*>(second.GetData())[17] != 0x6E)
        return fail(43, "MapSharedMem_existing", hr);

    if (FAILED(owner.Unmap())) return fail(44, "Owner_Unmap");
    if (static_cast<BYTE*>(reopened.GetData())[0] != 0x6E ||
        static_cast<BYTE*>(second.GetData())[17] != 0x6E)
        return fail(45, "SharedMem_independent_lifetime");
    if (FAILED(second.Unmap()) || FAILED(reopened.Unmap()))
        return fail(46, "SharedMem_teardown");

    if (SUCCEEDED(owner.MapSharedMem(0, name)) ||
        SUCCEEDED(owner.MapSharedMem(32, nullptr)) ||
        SUCCEEDED(owner.OpenMapping(name, 1)) ||
        owner.GetHandle() != NULL)
        return fail(47, "SharedMem_bad_inputs_and_closed_name");
    return 0;
}

int main() {
    static_assert(sizeof(void*) == 8, "Windows x64 and ARM64 only");
    ULONG_PTR context = 0;
    if (::CoGetContextToken(&context) != CO_E_NOTINITIALIZED)
        return fail(1, "No_COM_apartment_on_entry");
    if (int rc = file_map()) return rc;
    if (int rc = named_shared_memory()) return rc;
    if (::CoGetContextToken(&context) != CO_E_NOTINITIALIZED)
        return fail(2, "Unexpected_COM_initialization");
    std::puts("ATL_MAPPING_OK file=1 offset=1 copy=1 shared=1 cleanup=1 no_com=1");
    return 0;
}
