// OpenATL - local ATL-style Windows path conveniences.
// Independently authored for this project under Apache-2.0.
// No COM, registry, shlwapi linkage, or external ATL provider.
#ifndef OPENATL_ATLPATH_H
#define OPENATL_ATLPATH_H
#pragma once

#include <windows.h>
#include <tchar.h>
#include <string>

namespace ATL {

// A bounded subset of ATL's CPath conveniences. Path manipulation is lexical:
// it does not canonicalize '..', resolve symlinks or establish a security root.
// Append only accepts a relative component, never an absolute replacement.
class CPath {
public:
    CPath() = default;
    explicit CPath(LPCTSTR path) { SetPath(path); }

    void SetPath(LPCTSTR path) {
        if (path) path_.assign(path);
        else path_.clear();
    }
    LPCTSTR GetString() const noexcept { return path_.c_str(); }
    operator LPCTSTR() const noexcept { return GetString(); }
    bool IsEmpty() const noexcept { return path_.empty(); }

    bool IsRelative() const noexcept {
        if (path_.empty()) return true;
        if (Separator(path_[0])) return false;
        return !(path_.size() >= 2 && path_[1] == _T(':'));
    }

    LPCTSTR GetFileName() const noexcept {
        const size_t slash = path_.find_last_of(_T("\\/"));
        return path_.c_str() + (slash == String::npos ? 0 : slash + 1);
    }

    LPCTSTR GetExtension() const noexcept {
        const size_t slash = path_.find_last_of(_T("\\/"));
        const size_t dot = path_.find_last_of(_T('.'));
        return path_.c_str() + (dot == String::npos ||
            (slash != String::npos && dot <= slash) ? path_.size() : dot);
    }

    BOOL RemoveExtension() {
        const size_t slash = path_.find_last_of(_T("\\/"));
        const size_t dot = path_.find_last_of(_T('.'));
        if (dot == String::npos || (slash != String::npos && dot <= slash))
            return FALSE;
        path_.erase(dot);
        return TRUE;
    }

    BOOL RemoveFileSpec() {
        const size_t slash = path_.find_last_of(_T("\\/"));
        if (slash == String::npos) return FALSE;
        // Preserve a drive or filesystem root rather than turning C:\file
        // into C: (a drive-relative path) or \file into an empty string.
        if (slash == 0) path_.erase(1);
        else if (slash == 2 && path_.size() >= 3 && path_[1] == _T(':'))
            path_.erase(3);
        else path_.erase(slash);
        return TRUE;
    }

    BOOL AddBackslash() {
        if (path_.empty()) return FALSE;
        if (!Separator(path_.back())) path_.push_back(_T('\\'));
        return TRUE;
    }

    BOOL Append(LPCTSTR component) {
        if (!component || !*component || Separator(component[0]) ||
            (component[0] && component[1] == _T(':')))
            return FALSE; // fail closed for rooted, UNC, or drive-qualified
        if (!path_.empty() && !Separator(path_.back()))
            path_.push_back(_T('\\'));
        path_.append(component);
        return TRUE;
    }

    bool FileExists() const noexcept {
        return !path_.empty() &&
            ::GetFileAttributes(path_.c_str()) != INVALID_FILE_ATTRIBUTES;
    }
    bool IsDirectory() const noexcept {
        if (path_.empty()) return false;
        const DWORD attributes = ::GetFileAttributes(path_.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES &&
               (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

private:
    using String = std::basic_string<TCHAR>;
    static bool Separator(TCHAR value) noexcept {
        return value == _T('\\') || value == _T('/');
    }
    String path_;
};

} // namespace ATL
#endif // OPENATL_ATLPATH_H
