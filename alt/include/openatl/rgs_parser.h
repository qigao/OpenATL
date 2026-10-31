// OpenATL - bounded, source-owned ATL RGS grammar parser (C++17).
// This is a read-only parser: HKCR is a symbolic AST root, never a Win32
// registry handle. ForceRemove is metadata, not permission to delete keys.
#ifndef OPENATL_RGS_PARSER_H
#define OPENATL_RGS_PARSER_H

#include <algorithm>
#include <cstddef>
#include <cwctype>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace OpenATL { namespace RGS {

enum class Error {
    Ok, Syntax, Unsupported, Limit, Duplicate, InvalidName, AllocationFailure
};
enum class Directive { Ordinary, NoRemove, ForceRemove };
struct Value {
    std::wstring name; // Empty name is a Win32 registry key's default value.
    std::wstring text; // Only ATL 's' (REG_SZ) values are accepted at present.
};
struct Key {
    std::wstring path; // Relative to a caller-owned Classes root; never HKCR.
    Directive directive = Directive::Ordinary;
    std::vector<Value> values;
};
struct Document {
    std::vector<Key> keys; // Parents precede their children.
};
struct Replacement { std::wstring name; std::wstring text; };

// Hard complexity ceilings. A resource exceeding any ceiling fails atomically;
// the caller's output Document is unchanged on every failure.
constexpr size_t MaxSourceUnits = 65536;
constexpr size_t MaxKeys = 512;
constexpr size_t MaxValues = 1024;
constexpr size_t MaxNesting = 20;
constexpr size_t MaxNameUnits = 128;
constexpr size_t MaxPathUnits = 2048;
constexpr size_t MaxValueUnits = 8192;
constexpr size_t MaxReplacements = 64;
constexpr size_t MaxReplacementNameUnits = 64;

namespace detail {
inline wchar_t Fold(wchar_t x) noexcept {
    return x >= L'A' && x <= L'Z' ? wchar_t(x + (L'a' - L'A')) : x;
}
inline bool Same(std::wstring_view a, std::wstring_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (Fold(a[i]) != Fold(b[i])) return false;
    return true;
}
inline bool Hex(wchar_t ch) noexcept {
    return (ch >= L'0' && ch <= L'9') ||
           (Fold(ch) >= L'a' && Fold(ch) <= L'f');
}
inline bool Guid(std::wstring_view s) noexcept {
    if (s.size() != 38 || s.front() != L'{' || s.back() != L'}') return false;
    for (size_t i = 1; i < 37; ++i) {
        if (i == 9 || i == 14 || i == 19 || i == 24) {
            if (s[i] != L'-') return false;
        } else if (!Hex(s[i])) return false;
    }
    return true;
}
inline bool Name(std::wstring_view s) noexcept {
    if (s.empty() || s.size() > MaxNameUnits || Same(s, L".") ||
        Same(s, L"..") || Same(s, L"OpenATL.COMOwner")) return false;
    if (s.front() == L'{' && s.back() == L'}') return Guid(s);
    for (wchar_t ch : s) {
        if ((ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z') ||
            (ch >= L'0' && ch <= L'9') || ch == L'.' || ch == L'_' ||
            ch == L'-' || ch == L' ') continue;
        return false;
    }
    return true;
}
inline size_t ModuleTokenLength(std::wstring_view value, size_t at) noexcept {
    if (at > value.size()) return 0;
    if (value.size() - at >= 12 && Same(value.substr(at, 12), L"%MODULE_RAW%")) return 12;
    if (value.size() - at >= 8 && Same(value.substr(at, 8), L"%MODULE%")) return 8;
    return 0;
}
inline bool ValidSubstitution(std::wstring_view value) noexcept {
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] != L'%') continue;
        const size_t length = ModuleTokenLength(value, i);
        if (!length) return false;
        i += length - 1;
    }
    return true;
}
} // namespace detail

// Replacement text is data within one key/name/value token, never RGS syntax.
// Do not recursively expand data or permit it to introduce a second % pass.
inline Error ValidateReplacements(const std::vector<Replacement>& map) noexcept {
    if (map.size() > MaxReplacements) return Error::Limit;
    size_t total = 0;
    for (size_t i = 0; i < map.size(); ++i) {
        const auto& item = map[i];
        if (item.name.empty() || item.name.size() > MaxReplacementNameUnits)
            return Error::InvalidName;
        for (size_t j = 0; j < item.name.size(); ++j) {
            const wchar_t ch = detail::Fold(item.name[j]);
            if ((ch >= L'a' && ch <= L'z') || ch == L'_' ||
                (j && ch >= L'0' && ch <= L'9')) continue;
            return Error::InvalidName;
        }
        if (detail::Same(item.name, L"MODULE") ||
            detail::Same(item.name, L"MODULE_RAW")) return Error::InvalidName;
        if (item.text.size() > MaxValueUnits) return Error::Limit;
        for (wchar_t ch : item.text)
            if (!ch || ch == L'%' || ch == L'\r' || ch == L'\n')
                return Error::Unsupported;
        total += item.name.size() + item.text.size();
        if (total > MaxSourceUnits) return Error::Limit;
        for (size_t j = 0; j < i; ++j)
            if (detail::Same(item.name, map[j].name)) return Error::Duplicate;
    }
    return Error::Ok;
}

class Parser final {
public:
    Error Parse(std::wstring_view source, Document& output,
                const std::vector<Replacement>* replacements = nullptr) noexcept {
        if (source.empty()) return Error::Syntax;
        if (source.size() > MaxSourceUnits) return Error::Limit;
        if (replacements) {
            const Error error = ValidateReplacements(*replacements);
            if (error != Error::Ok) return error;
        }
        try {
            source_ = source;
            replacements_ = replacements;
            at_ = 0;
            valueCount_ = 0;
            error_ = Error::Ok;
            document_ = Document{};
            Advance();
            if (error_ != Error::Ok) return error_;
            if (!IsWord(L"HKCR")) return Error::Unsupported;
            Advance();
            if (!Consume(Kind::Open)) return Status();
            if (!Entries(std::wstring{}, 0)) return Status();
            if (token_.type != Kind::End || document_.keys.empty())
                return Error::Syntax;
            output = std::move(document_);
            return Error::Ok;
        } catch (const std::bad_alloc&) { return Error::AllocationFailure; }
          catch (...) { return Error::Unsupported; }
    }
private:
    enum class Kind { End, Word, Quoted, Open, Close, Equal, Invalid };
    struct Token { Kind type = Kind::End; std::wstring text; } token_;
    std::wstring_view source_;
    size_t at_ = 0;
    size_t valueCount_ = 0;
    Error error_ = Error::Ok;
    Document document_;
    const std::vector<Replacement>* replacements_ = nullptr;

    bool ExpandToken(std::wstring& token) {
        std::wstring expanded;
        for (size_t i = 0; i < token.size();) {
            if (token[i] != L'%') { expanded += token[i++]; }
            else {
                const size_t end = token.find(L'%', i + 1);
                if (end == token.npos) { Fail(Error::Unsupported); return false; }
                const std::wstring_view name(token.data() + i + 1, end - i - 1);
                if (detail::Same(name, L"MODULE") || detail::Same(name, L"MODULE_RAW"))
                    expanded.append(token, i, end - i + 1);
                else {
                    const Replacement* found = nullptr;
                    if (replacements_)
                        for (const auto& entry : *replacements_)
                            if (detail::Same(name, entry.name)) { found = &entry; break; }
                    if (!found) { Fail(Error::Unsupported); return false; }
                    expanded += found->text;
                }
                i = end + 1;
            }
            if (expanded.size() > MaxValueUnits) { Fail(Error::Limit); return false; }
        }
        token = std::move(expanded);
        return true;
    }

    Error Status() const noexcept { return error_ == Error::Ok ? Error::Syntax : error_; }
    bool IsWord(std::wstring_view name) const noexcept {
        return token_.type == Kind::Word && detail::Same(token_.text, name);
    }
    void Fail(Error error) noexcept { if (error_ == Error::Ok) error_ = error; }
    bool Consume(Kind kind) {
        if (token_.type != kind) { Fail(Error::Syntax); return false; }
        Advance(); return error_ == Error::Ok;
    }
    void Advance() {
        if (error_ != Error::Ok) return;
        token_ = Token{};
        const auto n = source_.size();
        while (at_ < n) {
            wchar_t c = source_[at_];
            if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') { ++at_; continue; }
            if (c == L';' || (c == L'/' && at_ + 1 < n && source_[at_ + 1] == L'/')) {
                while (at_ < n && source_[at_] != L'\n') ++at_;
                continue;
            }
            break;
        }
        if (at_ == n) return;
        wchar_t ch = source_[at_];
        // A complete {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx} denotes a key,
        // not the opening brace of a RGS scope.
        if (ch == L'{' && at_ + 38 <= n && detail::Guid(source_.substr(at_, 38))) {
            token_.type = Kind::Word;
            token_.text.assign(source_.substr(at_, 38));
            at_ += 38;
            return;
        }
        if (ch == L'{' || ch == L'}' || ch == L'=') {
            token_.type = ch == L'{' ? Kind::Open : ch == L'}' ? Kind::Close : Kind::Equal;
            ++at_;
            return;
        }
        if (ch == L'\'') {
            token_.type = Kind::Quoted;
            ++at_;
            bool closed = false;
            while (at_ < n) {
                wchar_t value = source_[at_++];
                if (value == L'\'') {
                    if (at_ < n && source_[at_] == L'\'') { ++at_; token_.text += L'\''; }
                    else { closed = true; break; }
                } else if (value == L'\0' || value == L'\r' || value == L'\n') {
                    Fail(Error::Syntax); return;
                } else token_.text += value;
                if (token_.text.size() > MaxValueUnits) { Fail(Error::Limit); return; }
            }
            if (!closed) Fail(Error::Syntax);
            return;
        }
        if ((ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z') ||
            (ch >= L'0' && ch <= L'9') || ch == L'_' || ch == L'-' || ch == L'%') {
            token_.type = Kind::Word;
            size_t begin = at_++;
            while (at_ < n) {
                wchar_t c = source_[at_];
                if ((c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') ||
                    (c >= L'0' && c <= L'9') || c == L'.' || c == L'_' || c == L'-' || c == L'%')
                    ++at_;
                else break;
                if (at_ - begin > MaxNameUnits) { Fail(Error::Limit); return; }
            }
            token_.text.assign(source_.substr(begin, at_ - begin));
            return;
        }
        Fail(Error::Unsupported);
        token_.type = Kind::Invalid;
    }
    bool ReadStringValue(std::wstring& out) {
        if (!Consume(Kind::Equal)) return false;
        if (!IsWord(L"s")) { Fail(Error::Unsupported); return false; }
        Advance();
        if (token_.type != Kind::Quoted) { Fail(Error::Syntax); return false; }
        if (!ExpandToken(token_.text)) return false;
        if (!detail::ValidSubstitution(token_.text)) { Fail(Error::Unsupported); return false; }
        out = std::move(token_.text);
        Advance(); return error_ == Error::Ok;
    }
    bool RecordValue(size_t target, std::wstring name, std::wstring text) {
        if (valueCount_++ == MaxValues) { Fail(Error::Limit); return false; }
        for (const Value& prior : document_.keys[target].values) {
            if (detail::Same(prior.name, name)) { Fail(Error::Duplicate); return false; }
        }
        document_.keys[target].values.push_back({std::move(name), std::move(text)});
        return true;
    }
    bool Entries(std::wstring parent, size_t depth) {
        if (depth > MaxNesting) { Fail(Error::Limit); return false; }
        while (token_.type != Kind::Close) {
            if (token_.type == Kind::End) { Fail(Error::Syntax); return false; }
            Directive directive = Directive::Ordinary;
            if (IsWord(L"NoRemove")) { directive = Directive::NoRemove; Advance(); }
            else if (IsWord(L"ForceRemove")) { directive = Directive::ForceRemove; Advance(); }
            if (IsWord(L"val")) {
                if (directive != Directive::Ordinary || parent.empty() || document_.keys.empty()) {
                    Fail(Error::Syntax); return false;
                }
                Advance();
                if (token_.type != Kind::Word && token_.type != Kind::Quoted) {
                    Fail(Error::Syntax); return false;
                }
                std::wstring name = std::move(token_.text);
                if (!ExpandToken(name)) return false;
                if (!detail::Name(name)) { Fail(Error::InvalidName); return false; }
                Advance();
                std::wstring value;
                if (!ReadStringValue(value)) return false;
                size_t index = document_.keys.size();
                while (index != 0) {
                    --index;
                    if (detail::Same(document_.keys[index].path, parent))
                        break;
                }
                if (!detail::Same(document_.keys[index].path, parent)) {
                    Fail(Error::Syntax); return false;
                }
                if (!RecordValue(index, std::move(name), std::move(value))) return false;
                continue;
            }
            if (token_.type != Kind::Word && token_.type != Kind::Quoted) {
                Fail(Error::Syntax); return false;
            }
            std::wstring name = std::move(token_.text);
            if (!ExpandToken(name)) return false;
            if (!detail::Name(name)) { Fail(Error::InvalidName); return false; }
            Advance();
            std::wstring path;
            if (!parent.empty()) { path.assign(parent); path += L'\\'; }
            path += name;
            if (path.size() > MaxPathUnits) { Fail(Error::Limit); return false; }
            for (const Key& k : document_.keys) {
                if (detail::Same(k.path, path)) { Fail(Error::Duplicate); return false; }
            }
            if (document_.keys.size() == MaxKeys) { Fail(Error::Limit); return false; }
            size_t index = document_.keys.size();
            document_.keys.push_back({std::move(path), directive, {}});
            if (token_.type == Kind::Equal) {
                std::wstring value;
                if (!ReadStringValue(value) || !RecordValue(index, L"", std::move(value)))
                    return false;
            }
            if (token_.type == Kind::Open) {
                Advance();
                if (!Entries(document_.keys[index].path, depth + 1)) return false;
            }
        }
        Advance();
        return error_ == Error::Ok;
    }
};

// Produce a stable expanded plan without mutating the original document.
// The calling installer must still validate module path ownership itself.
inline Error ExpandModule(std::wstring_view text, std::wstring_view module,
                          std::wstring& output, bool quoteModule = false) noexcept;

inline Error ResolveModule(const Document& source, std::wstring_view module,
                           Document& output, bool quoteModule = false) noexcept {
    try {
        Document expanded = source;
        for (Key& key : expanded.keys) {
            for (Value& value : key.values) {
                std::wstring text;
                Error e = ExpandModule(value.text, module, text, quoteModule);
                if (e != Error::Ok) return e;
                value.text = std::move(text);
            }
        }
        output = std::move(expanded);
        return Error::Ok;
    } catch (const std::bad_alloc&) { return Error::AllocationFailure; }
      catch (...) { return Error::Unsupported; }
}

inline Error Parse(std::wstring_view text, Document& output) noexcept {
    Parser parser;
    return parser.Parse(text, output);
}
inline Error Parse(std::wstring_view text, const std::vector<Replacement>& map,
                   Document& output) noexcept {
    Parser parser;
    return parser.Parse(text, output, &map);
}

// Substitute MODULE/MODULE_RAW after custom token replacements. Only MODULE
// receives the selected quoting policy; the canonical path itself is unchanged.
// Expansion is an explicit,
// bounded operation: no environment variables, registry/COM access or shell.
inline Error ExpandModule(std::wstring_view text, std::wstring_view module,
                          std::wstring& output, bool quoteModule) noexcept {
    if (module.empty() || module.size() > MaxPathUnits) return Error::InvalidName;
    if (!detail::ValidSubstitution(text)) return Error::Unsupported;
    try {
        std::wstring expanded;
        for (size_t i = 0; i < text.size();) {
            if (text[i] == L'%') {
                const size_t length = detail::ModuleTokenLength(text, i);
                const bool quoted = quoteModule && length == 8;
                if (quoted) expanded += L'"';
                expanded.append(module);
                if (quoted) expanded += L'"';
                i += length;
            }
            else { expanded += text[i++]; }
            if (expanded.size() > MaxValueUnits) return Error::Limit;
        }
        output = std::move(expanded);
        return Error::Ok;
    } catch (const std::bad_alloc&) { return Error::AllocationFailure; }
      catch (...) { return Error::Unsupported; }
}

}} // namespace OpenATL::RGS
#endif // OPENATL_RGS_PARSER_H
