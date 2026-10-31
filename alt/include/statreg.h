// Static ATL registrar facade. Owned COM interfaces have independent lifetime.
#ifndef OPENATL_STATREG_H
#define OPENATL_STATREG_H
#pragma once
#include "atlbase.h"

namespace ATL {
class CRegObject {
public:
    CRegObject() noexcept : status_(AtlCreateRegistrar(&registrar_.p)) {}
    CRegObject(const CRegObject&) = delete;
    CRegObject& operator=(const CRegObject&) = delete;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** output) noexcept {
        if (!output) return E_POINTER;
        *output = nullptr;
        return registrar_.p ? registrar_->QueryInterface(iid, output) : Failure();
    }
    HRESULT STDMETHODCALLTYPE AddReplacement(LPCOLESTR key, LPCOLESTR value) noexcept {
        return registrar_.p ? registrar_->AddReplacement(key, value) : Failure();
    }
    HRESULT STDMETHODCALLTYPE ClearReplacements() noexcept {
        return registrar_.p ? registrar_->ClearReplacements() : Failure();
    }
    HRESULT STDMETHODCALLTYPE StringRegister(LPCOLESTR source) noexcept {
        return registrar_.p ? registrar_->StringRegister(source) : Failure();
    }
    HRESULT STDMETHODCALLTYPE StringUnregister(LPCOLESTR source) noexcept {
        return registrar_.p ? registrar_->StringUnregister(source) : Failure();
    }
    HRESULT STDMETHODCALLTYPE FileRegister(LPCOLESTR path) noexcept {
        return registrar_.p ? registrar_->FileRegister(path) : Failure();
    }
    HRESULT STDMETHODCALLTYPE FileUnregister(LPCOLESTR path) noexcept {
        return registrar_.p ? registrar_->FileUnregister(path) : Failure();
    }
    HRESULT STDMETHODCALLTYPE ResourceRegisterSz(LPCOLESTR path, LPCOLESTR name, LPCOLESTR type) noexcept {
        return registrar_.p ? registrar_->ResourceRegisterSz(path, name, type) : Failure();
    }
    HRESULT STDMETHODCALLTYPE ResourceUnregisterSz(LPCOLESTR path, LPCOLESTR name, LPCOLESTR type) noexcept {
        return registrar_.p ? registrar_->ResourceUnregisterSz(path, name, type) : Failure();
    }
    HRESULT STDMETHODCALLTYPE ResourceRegister(LPCOLESTR path, UINT id, LPCOLESTR type) noexcept {
        return registrar_.p ? registrar_->ResourceRegister(path, id, type) : Failure();
    }
    HRESULT STDMETHODCALLTYPE ResourceUnregister(LPCOLESTR path, UINT id, LPCOLESTR type) noexcept {
        return registrar_.p ? registrar_->ResourceUnregister(path, id, type) : Failure();
    }
private:
    HRESULT Failure() const noexcept { return FAILED(status_) ? status_ : E_UNEXPECTED; }
    CComPtr<IRegistrar> registrar_;
    HRESULT status_;
};
} // namespace ATL
#endif
