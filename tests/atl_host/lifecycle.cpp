// Registered COM factories and actual HWNDs exercise host transaction ordering
// and reentrant retirement without relying on a particular installed control.
#include <atlhost.h>
#include <cstdio>
#include <new>

#define REQUIRE(x) do { if (!(x)) { std::fprintf(stderr, "HOST_LIFECYCLE_FAIL line=%d: %s\n", __LINE__, #x); return false; } } while (0)
namespace {
template<class T> class OwnedWindow : public T {
public:
    ~OwnedWindow() { if (::IsWindow(this->m_hWnd)) ::DestroyWindow(this->m_hWnd); }
};
struct ControlState {
    CLSID classId{};
    bool siteFirst = true, initInterface = true, failInit = false, destroyDuringInit = false;
    bool siteOrder = true, licenseSeen = false;
    HWND window = nullptr;
    int initialized = 0, loaded = 0, activated = 0, detached = 0, closed = 0, destroyed = 0;
};
class Control : public IOleObject, public IPersistStreamInit, public IPersistStream, public IRunnableObject {
public:
    explicit Control(ControlState& state) : state_(state) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        if (iid == IID_IUnknown || iid == IID_IOleObject) *value = static_cast<IOleObject*>(this);
        else if (iid == IID_IPersistStreamInit && state_.initInterface) *value = static_cast<IPersistStreamInit*>(this);
        else if (iid == IID_IPersist || iid == IID_IPersistStream) *value = static_cast<IPersistStream*>(this);
        else if (iid == IID_IRunnableObject) *value = static_cast<IRunnableObject*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --refs_; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE SetClientSite(IOleClientSite* site) override {
        if (!site && site_) ++state_.detached;
        site_ = site; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetClientSite(IOleClientSite** site) override { return site_.CopyTo(site); }
    HRESULT STDMETHODCALLTYPE SetHostNames(LPCOLESTR, LPCOLESTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Close(DWORD) override { ++state_.closed; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetMoniker(DWORD, IMoniker*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetMoniker(DWORD, DWORD, IMoniker**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE InitFromData(IDataObject*, BOOL, DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetClipboardData(DWORD, IDataObject**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE DoVerb(LONG, LPMSG, IOleClientSite* site, LONG, HWND parent, LPCRECT) override {
        if (parent != state_.window || site != site_.p) return E_FAIL;
        ++state_.activated; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EnumVerbs(IEnumOLEVERB**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Update() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE IsUpToDate() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetUserClassID(CLSID* id) override { return GetClassID(id); }
    HRESULT STDMETHODCALLTYPE GetUserType(DWORD, LPOLESTR*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetExtent(DWORD, SIZEL*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetExtent(DWORD, SIZEL*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Advise(IAdviseSink*, DWORD*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Unadvise(DWORD) override { return OLE_E_NOCONNECTION; }
    HRESULT STDMETHODCALLTYPE EnumAdvise(IEnumSTATDATA**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetMiscStatus(DWORD, DWORD* misc) override { if (!misc) return E_POINTER; *misc = state_.siteFirst ? OLEMISC_SETCLIENTSITEFIRST : 0; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetColorScheme(LOGPALETTE*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetClassID(CLSID* id) override { if (!id) return E_POINTER; *id = state_.classId; return S_OK; }
    HRESULT STDMETHODCALLTYPE IsDirty() override { return S_FALSE; }
    HRESULT STDMETHODCALLTYPE Load(IStream* stream) override {
        ULONG read = 0; DWORD marker = 0;
        if (!stream || stream->Read(&marker, sizeof(marker), &read) != S_OK || read != sizeof(marker) || marker != 42) return E_FAIL;
        ++state_.loaded; return Initialize();
    }
    HRESULT STDMETHODCALLTYPE Save(IStream*, BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetSizeMax(ULARGE_INTEGER* size) override { if (!size) return E_POINTER; size->QuadPart = 4; return S_OK; }
    HRESULT STDMETHODCALLTYPE InitNew() override { ++state_.initialized; return Initialize(); }
    HRESULT STDMETHODCALLTYPE GetRunningClass(CLSID* id) override { return GetClassID(id); }
    HRESULT STDMETHODCALLTYPE Run(IBindCtx*) override { return S_OK; }
    BOOL STDMETHODCALLTYPE IsRunning() override { return TRUE; }
    HRESULT STDMETHODCALLTYPE LockRunning(BOOL, BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetContainedObject(BOOL) override { return S_OK; }
private:
    HRESULT Initialize() {
        state_.siteOrder = state_.siteOrder && (static_cast<bool>(site_.p) == state_.siteFirst);
        if (state_.destroyDuringInit) ::DestroyWindow(state_.window);
        return state_.failInit || !state_.siteOrder ? E_FAIL : S_OK;
    }
    ~Control() { ++state_.destroyed; }
    ULONG refs_ = 1;
    ControlState& state_;
    ATL::CComPtr<IOleClientSite> site_;
};
class Factory : public IClassFactory2 {
public:
    explicit Factory(ControlState& state) : state_(state) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        if (iid != IID_IUnknown && iid != IID_IClassFactory && iid != IID_IClassFactory2) return E_NOINTERFACE;
        *value = static_cast<IClassFactory2*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --refs_; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID iid, void** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        auto object = new (std::nothrow) Control(state_); if (!object) return E_OUTOFMEMORY;
        HRESULT hr = object->QueryInterface(iid, value); object->Release(); return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE GetLicInfo(LICINFO* info) override {
        if (!info) return E_POINTER; *info = {}; info->cbLicInfo = sizeof(*info); info->fRuntimeKeyAvail = TRUE; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE RequestLicKey(DWORD, BSTR* key) override {
        if (!key) return E_POINTER; *key = ::SysAllocString(L"OpenATL-test-license"); return *key ? S_OK : E_OUTOFMEMORY;
    }
    HRESULT STDMETHODCALLTYPE CreateInstanceLic(IUnknown* outer, IUnknown*, REFIID iid, BSTR key, void** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        if (!key || ::wcscmp(key, L"OpenATL-test-license")) return CLASS_E_NOTLICENSED;
        state_.licenseSeen = true; return CreateInstance(outer, iid, value);
    }
private:
    ~Factory() = default;
    ULONG refs_ = 1;
    ControlState& state_;
};
struct Registration {
    DWORD cookie = 0;
    ~Registration() { if (cookie) ::CoRevokeClassObject(cookie); }
};
bool creation() {
    ControlState state;
    CLSID classId{}; REQUIRE(::CoCreateGuid(&classId) == S_OK);
    wchar_t className[40]{}; REQUIRE(::StringFromGUID2(classId, className, 40) > 0);
    state.classId = classId;
    ATL::CComPtr<IClassFactory2> factory; factory.Attach(new Factory(state));
    Registration registration;
    REQUIRE(::CoRegisterClassObject(classId, factory, CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE, &registration.cookie) == S_OK);
    for (int mode = 0; mode < 6; ++mode) {
        state = ControlState{};
        state.classId = classId;
        state.siteFirst = mode != 1;
        state.failInit = mode == 2;
        state.destroyDuringInit = mode == 3;
        state.initInterface = mode != 5;
        OwnedWindow<ATL::CAxWindow2> window;
        RECT rect{0,0,100,100};
        REQUIRE(window.Create(nullptr, &rect, nullptr, WS_POPUP));
        state.window = window;
        ATL::CComPtr<IStream> stream;
        if (mode == 4 || mode == 5) {
            REQUIRE(::CreateStreamOnHGlobal(nullptr, TRUE, &stream) == S_OK);
            DWORD marker = 42; ULONG written = 0;
            REQUIRE(stream->Write(&marker, sizeof(marker), &written) == S_OK && written == sizeof(marker));
            LARGE_INTEGER start{}; REQUIRE(stream->Seek(start, STREAM_SEEK_SET, nullptr) == S_OK);
        }
        ATL::CComBSTR license(mode == 4 ? L"OpenATL-test-license" : nullptr);
        HRESULT hr = window.CreateControlLic(className, stream, nullptr, license);
        if (mode == 2 || mode == 3) {
            REQUIRE(FAILED(hr) && state.activated == 0);
            REQUIRE(state.detached == 1 && state.closed == 1 && state.destroyed == 1);
        } else {
            REQUIRE(hr == S_OK && state.siteOrder && state.activated == 1);
            REQUIRE((mode >= 4 ? state.loaded : state.initialized) == 1);
            if (mode == 4) REQUIRE(state.licenseSeen);
        }
        if (::IsWindow(window)) REQUIRE(window.DestroyWindow());
        window.m_hWnd = nullptr;
        REQUIRE(state.destroyed == 1);
    }
    return true;
}
struct ActiveState { bool inside = false, diedInside = false; int destroyed = 0, calls = 0; };
class Active : public IOleInPlaceActiveObject {
public:
    Active(ActiveState& state, IOleInPlaceFrame* frame, HWND window, bool destroy) : state_(state), frame_(frame), window_(window), destroy_(destroy) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        if (iid != IID_IUnknown && iid != IID_IOleWindow && iid != IID_IOleInPlaceActiveObject) return E_NOINTERFACE;
        *value = static_cast<IOleInPlaceActiveObject*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --refs_; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE GetWindow(HWND* window) override { if (!window) return E_POINTER; *window = window_; return S_OK; }
    HRESULT STDMETHODCALLTYPE ContextSensitiveHelp(BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE TranslateAccelerator(LPMSG) override {
        ActiveState& state = state_; // remains valid even if the old host deletes this
        state.inside = true; ++state.calls;
        if (destroy_) ::DestroyWindow(window_); else frame_->SetActiveObject(nullptr, nullptr);
        state.inside = false; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnFrameWindowActivate(BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDocWindowActivate(BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ResizeBorder(LPCRECT, IOleInPlaceUIWindow*, BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE EnableModeless(BOOL) override { return S_OK; }
private:
    ~Active() { state_.diedInside = state_.inside; ++state_.destroyed; }
    ULONG refs_ = 1;
    ActiveState& state_;
    IOleInPlaceFrame* frame_; // test keeps its frame reference through the callback
    HWND window_;
    bool destroy_;
};
bool accelerators() {
    for (int mode = 0; mode < 4; ++mode) {
        ActiveState state;
        OwnedWindow<ATL::CAxWindow> window;
        RECT rect{0,0,100,100}; REQUIRE(window.Create(nullptr, &rect, nullptr, WS_POPUP));
        ATL::CComPtr<IOleInPlaceFrame> frame; REQUIRE(window.QueryHost(&frame) == S_OK);
        ATL::CComPtr<IOleControlSite> site; REQUIRE(window.QueryHost(&site) == S_OK);
        auto active = new Active(state, frame, window, mode >= 2);
        REQUIRE(frame->SetActiveObject(active, nullptr) == S_OK); active->Release();
        MSG message{}; message.hwnd = window; message.message = WM_KEYDOWN; message.wParam = VK_TAB;
        if (mode % 2) REQUIRE(site->TranslateAccelerator(&message, 0) == S_OK);
        else REQUIRE(::SendMessageW(window, WM_FORWARDMSG, 0, reinterpret_cast<LPARAM>(&message)) == TRUE);
        REQUIRE(state.calls == 1 && state.destroyed == 1 && !state.diedInside);
        if (mode >= 2) {
            ActiveState lateState;
            auto late = new Active(lateState, frame, window, false);
            HRESULT lateResult = frame->SetActiveObject(late, nullptr);
            late->Release();
            frame->SetActiveObject(nullptr, nullptr);
            REQUIRE(lateResult == CO_E_OBJNOTCONNECTED && lateState.destroyed == 1);
        }
        if (::IsWindow(window)) REQUIRE(window.DestroyWindow());
        window.m_hWnd = nullptr;
    }
    return true;
}
}
bool RunHostLifecycleTests() {
    const bool created = creation();
    const bool active = accelerators();
    return created && active;
}
