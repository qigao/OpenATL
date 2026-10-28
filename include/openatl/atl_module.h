// Clean-room modern ATL application module subset. Included by atlbase.h.
#ifndef OPENATL_ATL_MODULE_H
#define OPENATL_ATL_MODULE_H
#include "../atlbase.h"
#include "com_server.h"
#include <atomic>
#include <mutex>
#include <optional>
#include <vector>
#include <limits>
#include <shellapi.h>

namespace OpenATL { namespace COM {
struct ClassObjectEntry { CLSID clsid; IUnknown* factory; };
}}

namespace ATL {
class CAtlModule;
// Construction/destruction require external quiescence, like ATL's image module.
inline CAtlModule* _pAtlModule = nullptr;
using _ATL_TERMFUNC = void (WINAPI*)(DWORD_PTR);

class CAtlModule {
public:
    CAtlModule() noexcept {
        if (_pAtlModule) std::terminate();
        _pAtlModule = this;
    }
    ~CAtlModule() noexcept {
        Term();
        if (_pAtlModule != this) std::terminate();
        _pAtlModule = nullptr;
    }
    CAtlModule(const CAtlModule&) = delete;
    CAtlModule& operator=(const CAtlModule&) = delete;
    LONG GetLockCount() const noexcept { return locks_.load(); }
    LONG Lock() noexcept {
        if (terminated_.load()) std::terminate();
        LONG value = locks_.load();
        for (;;) {
            if (value == (std::numeric_limits<LONG>::max)()) std::terminate();
            if (locks_.compare_exchange_weak(value, value + 1)) return value + 1;
        }
    }
    LONG Unlock() noexcept {
        LONG value = locks_.load();
        for (;;) {
            if (!value) std::terminate();
            if (locks_.compare_exchange_weak(value, value - 1)) return value - 1;
        }
    }
    HRESULT AddTermFunc(_ATL_TERMFUNC callback, DWORD_PTR argument) noexcept {
        if (!callback) return E_INVALIDARG;
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            if (terminated_.load()) return E_UNEXPECTED;
            if (callbacks_.size() == 1024) return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
            callbacks_.push_back({callback, argument});
            return S_OK;
        } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
          catch (...) { return E_UNEXPECTED; }
    }
    void Term() noexcept {
        std::vector<Termination> callbacks;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (terminated_.load()) return;
            if (locks_.load()) std::terminate();
            terminated_.store(true);
            callbacks.swap(callbacks_);
        }
        for (auto i = callbacks.rbegin(); i != callbacks.rend(); ++i)
            i->callback(i->argument);
    }
protected:
    bool IsTerminated() const noexcept { return terminated_.load(); }
private:
    struct Termination { _ATL_TERMFUNC callback; DWORD_PTR argument; };
    std::atomic<LONG> locks_{0};
    std::atomic<bool> terminated_{false};
    std::mutex mutex_;
    std::vector<Termination> callbacks_;
};

template<class T>
class CAtlModuleT : public CAtlModule {
public:
    // Persistent registration requires an application-owned installer policy.
    HRESULT RegisterServer(BOOL = FALSE, const CLSID* = nullptr) noexcept { return E_NOTIMPL; }
    HRESULT UnregisterServer(BOOL = FALSE, const CLSID* = nullptr) noexcept { return E_NOTIMPL; }
};

namespace detail {
inline HRESULT AtlModuleException() noexcept {
    try { throw; }
    catch (const CAtlException& e) { return FAILED(e.m_hr) ? e.m_hr : E_UNEXPECTED; }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return E_UNEXPECTED; }
}
}

// DLL module lifetime and standard ATL export forwarding.
//
// BindServer() is the current explicit-class-table bridge, not a claim that
// OBJECT_ENTRY_AUTO, RGS or type-library registration is implemented. The
// bound OpenATL::COM::Module and its static class table must outlive the DLL
// module and every factory/object/server lock. Binding is one-time; callers
// must coordinate DLL unload and configuration externally.
template<class T>
class ATL_NO_VTABLE CAtlDllModuleT : public CAtlModuleT<T> {
public:
    CAtlDllModuleT() noexcept = default;
    CAtlDllModuleT(const CAtlDllModuleT&) = delete;
    CAtlDllModuleT& operator=(const CAtlDllModuleT&) = delete;

    HRESULT BindServer(OpenATL::COM::Module& module) noexcept {
        if (this->IsTerminated()) return E_UNEXPECTED;
        const HRESULT status = module.Status();
        if (FAILED(status)) return status;
        OpenATL::COM::Module* expected = nullptr;
        if (server_.compare_exchange_strong(expected, &module)) return S_OK;
        return expected == &module ? S_OK : HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    }

    // No COM initialization/registry updates may run under the loader lock.
    BOOL WINAPI DllMain(DWORD /*reason*/, LPVOID /*reserved*/) noexcept {
        return TRUE;
    }

    HRESULT DllCanUnloadNow() const noexcept {
        if (this->IsTerminated() || this->GetLockCount() != 0) return S_FALSE;
        const auto* server = server_.load();
        return server && server->CanUnloadNow() != S_OK ? S_FALSE : S_OK;
    }

    HRESULT DllGetClassObject(REFCLSID clsid, REFIID iid, LPVOID* output) noexcept {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (this->IsTerminated()) return E_UNEXPECTED;
        // Match ATL's CRTP hook: the DLL export delegates to T::GetClassObject,
        // allowing a derived module to provide a standard object-map lookup.
        // Pin the image throughout this synchronous callback.
        this->Lock();
        HRESULT result = E_UNEXPECTED;
        try {
            result = static_cast<T*>(this)->GetClassObject(clsid, iid, output);
        } catch (...) {
            result = detail::AtlModuleException();
        }
        if (FAILED(result)) *output = nullptr;
        this->Unlock();
        return result;
    }

    HRESULT GetClassObject(REFCLSID clsid, REFIID iid, LPVOID* output) noexcept {
        if (!output) return E_POINTER;
        *output = nullptr;
        if (this->IsTerminated()) return E_UNEXPECTED;
        auto* server = server_.load();
        return server ? server->GetClassObject(clsid, iid, output)
                      : CLASS_E_CLASSNOTAVAILABLE;
    }

    // Default registration remains E_NOTIMPL until a derived module supplies
    // real registry/type-library semantics. Do not report fake S_OK.
    // Pin active registration exports against a cooperative DLL unload.
    HRESULT DllRegisterServer(BOOL registerTypeLib = TRUE) noexcept {
        if (this->IsTerminated()) return E_UNEXPECTED;
        this->Lock();
        HRESULT result = E_UNEXPECTED;
        try {
            result = static_cast<T*>(this)->RegisterServer(registerTypeLib, nullptr);
        } catch (...) {
            result = detail::AtlModuleException();
        }
        this->Unlock();
        return result;
    }
    HRESULT DllUnregisterServer(BOOL unregisterTypeLib = TRUE) noexcept {
        if (this->IsTerminated()) return E_UNEXPECTED;
        this->Lock();
        HRESULT result = E_UNEXPECTED;
        try {
            result = static_cast<T*>(this)->UnregisterServer(unregisterTypeLib, nullptr);
        } catch (...) {
            result = detail::AtlModuleException();
        }
        this->Unlock();
        return result;
    }

private:
    std::atomic<OpenATL::COM::Module*> server_{nullptr};
};

template<class T>
class CAtlExeModuleT : public CAtlModuleT<T> {
public:
    CAtlExeModuleT() noexcept : owner_(::GetCurrentThreadId()), m_hr(T::InitializeCom()) {}
    ~CAtlExeModuleT() noexcept {
        if (::GetCurrentThreadId() != owner_ || FAILED(RevokeClassObjects())) std::terminate();
        classes_.clear();
        this->Term();
        if (SUCCEEDED(m_hr)) T::UninitializeCom();
    }
    static HRESULT InitializeCom() noexcept {
#if defined(_ATL_APARTMENT_THREADED) || defined(_ATL_SINGLE_THREADED)
        return ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
#else
        return ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif
    }
    static void UninitializeCom() noexcept { ::CoUninitialize(); }
    HRESULT GetInitHRESULT() const noexcept { return m_hr; }

    HRESULT SetClassObjects(const OpenATL::COM::ClassObjectEntry* entries, size_t count) noexcept {
        HRESULT hr = CheckThread();
        if (FAILED(hr)) return hr;
        if (running_ || !cookies_.empty()) return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
        if (count > 1024) return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
        if (count && !entries) return E_INVALIDARG;
        try {
            std::vector<BoundClass> staged;
            staged.reserve(count);
            for (size_t i = 0; i < count; ++i) {
                if (!entries[i].factory || IsEqualCLSID(entries[i].clsid, CLSID_NULL)) return E_INVALIDARG;
                for (size_t j = 0; j < i; ++j)
                    if (IsEqualCLSID(entries[i].clsid, entries[j].clsid)) return E_INVALIDARG;
                BoundClass entry;
                entry.clsid = entries[i].clsid;
                hr = entries[i].factory->QueryInterface(IID_IClassFactory,
                    reinterpret_cast<void**>(&entry.factory));
                if (FAILED(hr)) return hr;
                if (!entry.factory) return E_UNEXPECTED;
                staged.push_back(std::move(entry));
            }
            classes_.swap(staged);
            return S_OK;
        } catch (...) { return detail::AtlModuleException(); }
    }
    HRESULT RegisterClassObjects(DWORD context, DWORD flags) noexcept {
        HRESULT hr = CheckThread();
        if (FAILED(hr)) return hr;
        if (!cookies_.empty()) return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
        if (classes_.empty()) return S_FALSE;
        try { cookies_.resize(classes_.size()); }
        catch (...) { return detail::AtlModuleException(); }
        for (size_t i = 0; i < classes_.size(); ++i) {
            DWORD cookie = 0;
            hr = ::CoRegisterClassObject(classes_[i].clsid, classes_[i].factory,
                                         context, flags, &cookie);
            if (FAILED(hr)) {
                (void)RevokeClassObjects(); // Retain failed-revocation cookies for retry.
                return hr;
            }
            cookies_[i] = cookie;
        }
        return S_OK;
    }
    HRESULT RevokeClassObjects() noexcept {
        if (cookies_.empty()) return S_FALSE;
        if (::GetCurrentThreadId() != owner_) return RPC_E_WRONG_THREAD;
        HRESULT result = S_OK;
        for (auto i = cookies_.rbegin(); i != cookies_.rend(); ++i) {
            if (!i->has_value()) continue;
            HRESULT hr = ::CoRevokeClassObject(**i);
            if (SUCCEEDED(hr)) i->reset();
            else if (SUCCEEDED(result)) result = hr;
        }
        if (SUCCEEDED(result)) cookies_.clear();
        return result;
    }
    HRESULT PreMessageLoop(int) noexcept {
        return static_cast<T*>(this)->RegisterClassObjects(CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE);
    }
    HRESULT PostMessageLoop() noexcept { return static_cast<T*>(this)->RevokeClassObjects(); }
    void RunMessageLoop() noexcept {
        MSG message{};
        BOOL result;
        while ((result = ::GetMessageW(&message, nullptr, 0, 0)) > 0) {
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }
        if (result == -1) {
            DWORD error = ::GetLastError();
            loopResult_ = error ? HRESULT_FROM_WIN32(error) : E_UNEXPECTED;
        }
    }
    HRESULT Run(int show = SW_HIDE) noexcept {
        HRESULT hr = CheckThread();
        if (FAILED(hr)) return hr;
        if (running_) return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
        running_ = true;
        loopResult_ = S_OK;
        try {
            hr = static_cast<T*>(this)->PreMessageLoop(show);
            if (hr == S_OK) {
                try { static_cast<T*>(this)->RunMessageLoop(); }
                catch (...) { loopResult_ = detail::AtlModuleException(); }
                hr = static_cast<T*>(this)->PostMessageLoop();
                if (FAILED(loopResult_)) hr = loopResult_;
            }
        } catch (...) { hr = detail::AtlModuleException(); }
        HRESULT cleanup = RevokeClassObjects();
        if (SUCCEEDED(hr) && FAILED(cleanup)) hr = cleanup;
        running_ = false;
        return hr;
    }
    bool ParseCommandLine(LPCTSTR commandLine, HRESULT* result) noexcept {
        if (!result) return false;
        *result = CheckThread();
        if (FAILED(*result)) return false;
        *result = E_INVALIDARG;
        if (!commandLine) return false;
        size_t length = 0;
        while (length < 32767 && commandLine[length]) ++length;
        if (length == 32767) { *result = HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW); return false; }
        int count = 0;
        LPWSTR* args = ::CommandLineToArgvW(commandLine, &count);
        if (!args) { *result = E_OUTOFMEMORY; return false; }
        bool run = true;
        *result = S_OK;
        try {
            for (int i = 1; i < count; ++i) {
                const wchar_t* option = args[i];
                if (*option != L'/' && *option != L'-') continue;
                ++option;
                if (::_wcsicmp(option, L"RegServer") == 0) {
                    *result = static_cast<T*>(this)->RegisterServer(TRUE); run = false; break;
                }
                if (::_wcsicmp(option, L"UnregServer") == 0) {
                    *result = static_cast<T*>(this)->UnregisterServer(TRUE); run = false; break;
                }
                if (::_wcsicmp(option, L"RegServerPerUser") == 0 ||
                    ::_wcsicmp(option, L"UnregServerPerUser") == 0) {
                    *result = E_NOTIMPL; run = false; break;
                }
            }
        } catch (...) { *result = detail::AtlModuleException(); run = false; }
        ::LocalFree(args);
        return run;
    }
    int WinMain(int show) noexcept {
        const HRESULT state = CheckThread();
        if (FAILED(state)) return static_cast<int>(state);
        HRESULT result = S_OK;
        try {
            if (static_cast<T*>(this)->ParseCommandLine(::GetCommandLineW(), &result))
                result = static_cast<T*>(this)->Run(show);
        } catch (...) { result = detail::AtlModuleException(); }
        return static_cast<int>(result);
    }
private:
    HRESULT CheckThread() const noexcept {
        if (::GetCurrentThreadId() != owner_) return RPC_E_WRONG_THREAD;
        if (FAILED(m_hr)) return m_hr;
        return this->IsTerminated() ? E_UNEXPECTED : S_OK;
    }
    struct BoundClass { CLSID clsid{}; CComPtr<IClassFactory> factory; };
    DWORD owner_;
    HRESULT m_hr;
    bool running_ = false;
    HRESULT loopResult_ = S_OK;
    std::vector<BoundClass> classes_;
    std::vector<std::optional<DWORD>> cookies_;
};
} // namespace ATL
#endif
