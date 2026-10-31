// OpenWTL built-in registrar. One parser/executor; no ATL DLL activation.
#ifndef OPENATL_ATL_REGISTRAR_H
#define OPENATL_ATL_REGISTRAR_H
#include "../atlbase.h"

namespace ATL { namespace detail {
class ScriptRegistrar final : public IRegistrar {
public:
    explicit ScriptRegistrar(CAtlModule* owner) noexcept : pin_(owner) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) noexcept override {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (!::IsEqualIID(iid, IID_IUnknown) && !::IsEqualIID(iid, IID_IRegistrarBase) &&
            !::IsEqualIID(iid, IID_IRegistrar)) return E_NOINTERFACE;
        *output = static_cast<IRegistrar*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override {
        if (!OpenATL::COM::detail::retain(references_)) std::terminate();
        return references_.load();
    }
    ULONG STDMETHODCALLTYPE Release() noexcept override {
        const ULONG count = OpenATL::COM::detail::release(references_);
        if (!count) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE AddReplacement(LPCOLESTR name, LPCOLESTR value) noexcept override {
        return map_.Add(name, value, true);
    }
    HRESULT STDMETHODCALLTYPE ClearReplacements() noexcept override { return map_.Clear(); }
    HRESULT STDMETHODCALLTYPE StringRegister(LPCOLESTR source) noexcept override {
        return String(source, TRUE);
    }
    HRESULT STDMETHODCALLTYPE StringUnregister(LPCOLESTR source) noexcept override {
        return String(source, FALSE);
    }
    HRESULT STDMETHODCALLTYPE FileRegister(LPCOLESTR path) noexcept override { return File(path, TRUE); }
    HRESULT STDMETHODCALLTYPE FileUnregister(LPCOLESTR path) noexcept override { return File(path, FALSE); }
    HRESULT STDMETHODCALLTYPE ResourceRegisterSz(LPCOLESTR path, LPCOLESTR name,
                                                  LPCOLESTR type) noexcept override {
        return Resource(path, name, type, TRUE);
    }
    HRESULT STDMETHODCALLTYPE ResourceUnregisterSz(LPCOLESTR path, LPCOLESTR name,
                                                    LPCOLESTR type) noexcept override {
        return Resource(path, name, type, FALSE);
    }
    HRESULT STDMETHODCALLTYPE ResourceRegister(LPCOLESTR path, UINT id,
                                                LPCOLESTR type) noexcept override {
        if (!id || id > 0xffff) return E_INVALIDARG;
        return Resource(path, MAKEINTRESOURCEW(id), type, TRUE);
    }
    HRESULT STDMETHODCALLTYPE ResourceUnregister(LPCOLESTR path, UINT id,
                                                  LPCOLESTR type) noexcept override {
        if (!id || id > 0xffff) return E_INVALIDARG;
        return Resource(path, MAKEINTRESOURCEW(id), type, FALSE);
    }
private:
    ~ScriptRegistrar() = default;
    static HMODULE Image() noexcept {
        const auto* context = OpenATL::COM::rgs_resource_detail::Current();
        if (context) return context->resourceModule;
        const auto* module = OpenATL::COM::rgs_resource_detail::CurrentModule();
        return module ? module->ResourceModule() : _AtlBaseModule.GetModuleInstance();
    }
    static HRESULT Execute(const OpenATL::RGS::Document& document, BOOL install,
                           bool perUser, std::wstring_view module) noexcept {
        if (OpenATL::COM::rgs_resource_detail::Current() ||
            OpenATL::COM::rgs_resource_detail::CurrentModule())
            return OpenATL::COM::ApplyRgsDocument(document, install, module);
        OpenATL::COM::ScopedModuleRgsRegistration scope(Image(), perUser);
        return OpenATL::COM::ApplyRgsDocument(document, install, module);
    }
    HRESULT ParseAndExecute(std::wstring_view source,
             const std::vector<OpenATL::RGS::Replacement>& map, BOOL install,
             bool perUser, std::wstring_view module) noexcept {
        OpenATL::RGS::Document document;
        const HRESULT hr = OpenATL::COM::rgs_registration_detail::MapError(
            OpenATL::RGS::Parse(source, map, document));
        return FAILED(hr) ? hr : Execute(document, install, perUser, module);
    }
    HRESULT String(LPCOLESTR source, BOOL install) noexcept {
        const bool perUser = perUserRegistration.load();
        if (!OpenATL::COM::registration_detail::bounded(source, OpenATL::RGS::MaxSourceUnits) ||
            !*source) return E_INVALIDARG;
        std::vector<OpenATL::RGS::Replacement> map;
        std::wstring module;
        const HRESULT hr = map_.Snapshot(map, module);
        return FAILED(hr) ? hr : ParseAndExecute(source, map, install, perUser, module);
    }
    struct FileHandle {
        HANDLE value = INVALID_HANDLE_VALUE;
        FileHandle() = default;
        FileHandle(const FileHandle&) = delete;
        FileHandle& operator=(const FileHandle&) = delete;
        ~FileHandle() { if (value != INVALID_HANDLE_VALUE) ::CloseHandle(value); }
    };
    static HRESULT ReadFileText(LPCOLESTR path, std::wstring& text) noexcept {
        if (!OpenATL::COM::registration_detail::bounded(path, OpenATL::RGS::MaxPathUnits))
            return E_INVALIDARG;
        try {
            std::wstring canonical;
            HRESULT hr = OpenATL::COM::rgs_registration_detail::CanonicalModule(path, canonical);
            if (FAILED(hr)) return hr;
            FileHandle file;
            file.value = ::CreateFileW(canonical.c_str(), GENERIC_READ, FILE_SHARE_READ,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
            if (file.value == INVALID_HANDLE_VALUE)
                return OpenATL::COM::rgs_resource_detail::Win32Failure(ERROR_OPEN_FAILED);
            if (::GetFileType(file.value) != FILE_TYPE_DISK)
                return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
            BY_HANDLE_FILE_INFORMATION info{};
            if (!::GetFileInformationByHandle(file.value, &info))
                return OpenATL::COM::rgs_resource_detail::Win32Failure(ERROR_READ_FAULT);
            if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return E_INVALIDARG;
            LARGE_INTEGER size{};
            if (!::GetFileSizeEx(file.value, &size))
                return OpenATL::COM::rgs_resource_detail::Win32Failure(ERROR_READ_FAULT);
            if (size.QuadPart <= 0) return E_INVALIDARG;
            if (size.QuadPart > OpenATL::RGS::MaxSourceUnits * 4 + 4)
                return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
            std::vector<BYTE> bytes(static_cast<size_t>(size.QuadPart));
            size_t at = 0;
            while (at < bytes.size()) {
                DWORD read = 0;
                if (!::ReadFile(file.value, bytes.data() + at,
                        static_cast<DWORD>(bytes.size() - at), &read, nullptr))
                    return OpenATL::COM::rgs_resource_detail::Win32Failure(ERROR_READ_FAULT);
                if (!read) return HRESULT_FROM_WIN32(ERROR_HANDLE_EOF);
                at += read;
            }
            BYTE extra = 0;
            DWORD read = 0;
            if (!::ReadFile(file.value, &extra, 1, &read, nullptr))
                return OpenATL::COM::rgs_resource_detail::Win32Failure(ERROR_READ_FAULT);
            if (read) return HRESULT_FROM_WIN32(ERROR_FILE_INVALID);
            return OpenATL::COM::rgs_resource_detail::Decode(bytes.data(), bytes.size(), text);
        } catch (...) { return AtlModuleException(); }
    }
    HRESULT File(LPCOLESTR path, BOOL install) noexcept {
        const bool perUser = perUserRegistration.load();
        std::vector<OpenATL::RGS::Replacement> map;
        std::wstring module;
        HRESULT hr = map_.Snapshot(map, module);
        if (FAILED(hr)) return hr;
        std::wstring source;
        hr = ReadFileText(path, source);
        return FAILED(hr) ? hr : ParseAndExecute(source, map, install, perUser, module);
    }
    HRESULT Resource(LPCOLESTR path, LPCOLESTR name, LPCOLESTR type, BOOL install) noexcept {
        const bool perUser = perUserRegistration.load();
        if (!OpenATL::COM::registration_detail::bounded(path, OpenATL::RGS::MaxPathUnits) ||
            !name || (!IS_INTRESOURCE(name) &&
            (!OpenATL::COM::registration_detail::bounded(name, OpenATL::RGS::MaxNameUnits) || !*name)) ||
            !type || IS_INTRESOURCE(type) ||
            !OpenATL::COM::registration_detail::bounded(type, OpenATL::RGS::MaxNameUnits))
            return E_INVALIDARG;
        if (!OpenATL::RGS::detail::Same(type, L"REGISTRY"))
            return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
        try {
            std::wstring requested;
            HRESULT hr = OpenATL::COM::rgs_registration_detail::CanonicalModule(path, requested);
            if (FAILED(hr)) return hr;
            std::vector<OpenATL::RGS::Replacement> map;
            std::wstring module;
            hr = map_.Snapshot(map, module);
            if (FAILED(hr)) return hr;
            // Hold a read-only handle through execution: the resource file
            // cannot be written or replaced between parsing and registry work.
            FileHandle file;
            file.value = ::CreateFileW(requested.c_str(), GENERIC_READ, FILE_SHARE_READ,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file.value == INVALID_HANDLE_VALUE)
                return OpenATL::COM::rgs_resource_detail::Win32Failure(ERROR_OPEN_FAILED);
            if (::GetFileType(file.value) != FILE_TYPE_DISK)
                return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
            struct ResourceImage {
                HMODULE value = nullptr;
                ~ResourceImage() { if (value) ::FreeLibrary(value); }
            } image;
            image.value = ::LoadLibraryExW(requested.c_str(), nullptr,
                LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
            if (!image.value)
                return OpenATL::COM::rgs_resource_detail::Win32Failure(ERROR_BAD_EXE_FORMAT);
            OpenATL::RGS::Document document;
            hr = OpenATL::COM::ReadRgsResource(image.value, name, document, &map);
            if (FAILED(hr)) return hr;
            // Resource source and server identity may differ only through an
            // explicit owned MODULE binding; otherwise the resource path owns
            // both MODULE expansion and the verified registration marker.
            return Execute(document, install, perUser, module.empty() ? requested : module);
        } catch (...) { return AtlModuleException(); }
    }
    RegistrarModulePin pin_; // Kept until after map destruction.
    std::atomic<ULONG> references_{1};
    RgsReplacementMap map_;
};
}} // namespace ATL::detail

namespace ATL {
inline HRESULT WINAPI AtlCreateRegistrar(IRegistrar** output) noexcept {
    if (!output) return E_POINTER;
    *output = nullptr;
    if (_pAtlModule && _pAtlModule->IsTerminated()) return E_UNEXPECTED;
    try {
        *output = new (std::nothrow) detail::ScriptRegistrar(_pAtlModule);
        return *output ? S_OK : E_OUTOFMEMORY;
    } catch (...) { return detail::AtlModuleException(); }
}
} // namespace ATL
#endif
