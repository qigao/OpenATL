// Source-only ATL/RGS parser admission. Original fixtures are from pinned
// Microsoft VCSamples; generated wide literals retain their bytes verbatim.
#include <openatl/rgs_parser.h>
#include "openatl_rgs_fixtures.generated.h"
#include <cstdio>
#include <string>
#include <string_view>

namespace {
using namespace OpenATL::RGS;
#define RGS_CHECK(x) do { if (!(x)) { std::fprintf(stderr, "RGS_PARSE_FAIL line=%d expr=%s\n", __LINE__, #x); return false; } } while (0)
const Key* Find(const Document& doc, std::wstring_view path) {
    for (const Key& key : doc.keys)
        if (detail::Same(key.path, path)) return &key;
    return nullptr;
}
const Value* FindValue(const Key& key, std::wstring_view name) {
    for (const Value& v : key.values)
        if (detail::Same(v.name, name)) return &v;
    return nullptr;
}

bool Originals() {
    constexpr size_t expectedKeys[] = {9, 9, 12, 12};
    constexpr size_t expectedValues[] = {10, 10, 12, 12};
    size_t sampleIndex = 0;
    for (const auto& sample : OpenATLRgsSamples::All) {
        Document d;
        RGS_CHECK(Parse(sample.content, d) == Error::Ok);
        RGS_CHECK(d.keys.size() == expectedKeys[sampleIndex]);
        size_t values = 0, force = 0;
        for (const Key& key : d.keys) {
            values += key.values.size();
            force += key.directive == Directive::ForceRemove ? 1 : 0;
        }
        RGS_CHECK(values == expectedValues[sampleIndex] && force == 3);
        const Key* shared = Find(d, L"CLSID");
        RGS_CHECK(shared && shared->directive == Directive::NoRemove &&
                  shared->values.empty());
        const Key* inproc = nullptr;
        const Key* guid = nullptr;
        for (const Key& key : d.keys) {
            if (key.path.size() >= 14 && key.path.compare(key.path.size()-14, 14, L"InprocServer32") == 0)
                inproc = &key;
            if (key.path.find(L"CLSID\\{") == 0 &&
                key.path.find(L'\\', 6) == std::wstring::npos)
                guid = &key;
        }
        RGS_CHECK(inproc && guid);
        const Value* path = FindValue(*inproc, L"");
        const Value* model = FindValue(*inproc, L"ThreadingModel");
        RGS_CHECK(path && path->text == L"%MODULE%" && model);
        RGS_CHECK(detail::Same(model->text, sampleIndex < 2 ? L"both" : L"Apartment"));
        if (sampleIndex < 2) {
            const Value* appId = FindValue(*guid, L"AppID");
            RGS_CHECK(appId && appId->text.size() == 38);
        } else {
            RGS_CHECK(FindValue(*guid, L"LocalizedString") != nullptr);
            const Key* prog = nullptr;
            const Key* typeLib = nullptr;
            const Key* version = nullptr;
            for (const Key& key : d.keys) {
                if (key.path.find(L"\\Programmable") != std::wstring::npos) prog = &key;
                if (key.path.find(L"\\TypeLib") != std::wstring::npos) typeLib = &key;
                if (key.path.find(L"\\Version") != std::wstring::npos &&
                    key.path.find(L"\\VersionIndependentProgID") == std::wstring::npos)
                    version = &key;
            }
            RGS_CHECK(prog && typeLib && version && prog->values.empty());
            const Value* typeLibValue = FindValue(*typeLib, L"");
            const Value* versionValue = FindValue(*version, L"");
            RGS_CHECK(typeLibValue && versionValue && versionValue->text == L"1.0");
        }
        Document resolved;
        RGS_CHECK(ResolveModule(d, L"C:\\Program Files\\OpenATL\\server.dll", resolved) == Error::Ok);
        const Key* updated = Find(resolved, inproc->path);
        RGS_CHECK(updated && FindValue(*updated, L"") &&
                  FindValue(*updated, L"")->text == L"C:\\Program Files\\OpenATL\\server.dll");
        RGS_CHECK(FindValue(*inproc, L"")->text == L"%MODULE%"); // Source unchanged.
        ++sampleIndex;
    }
    RGS_CHECK(sampleIndex == 4);
    return true;
}

bool RejectMalformed() {
    Document d;
    RGS_CHECK(Parse(L"HKCR { A = s 'ok' }", d) == Error::Ok);
    RGS_CHECK(d.keys.size() == 1 && d.keys[0].path == L"A");
    auto reject = [&](std::wstring_view s, Error expected) {
        return Parse(s, d) == expected && d.keys.size() == 1 && d.keys[0].path == L"A";
    };
    RGS_CHECK(reject(L"HKLM { A = s 'no' }", Error::Unsupported));
    RGS_CHECK(reject(L"HKCR { A = d '42' }", Error::Unsupported));
    RGS_CHECK(reject(L"HKCR { A = s '%HOME%' }", Error::Unsupported));
    RGS_CHECK(reject(L"HKCR { A = s 'unterminated }", Error::Syntax));
    RGS_CHECK(reject(L"HKCR { A { val X = s 'ok' val x = s 'no' } }", Error::Duplicate));
    RGS_CHECK(reject(L"HKCR { A = s 'ok' a = s 'bad' }", Error::Duplicate));
    RGS_CHECK(reject(L"HKCR { OpenATL.COMOwner = s 'forbidden' }", Error::InvalidName));
    RGS_CHECK(reject(L"HKCR { A { val OpenATL.COMOwner = s 'reserved' } }", Error::InvalidName));
    RGS_CHECK(reject(L"HKCR { .. = s 'traversal' }", Error::Unsupported));
    RGS_CHECK(reject(std::wstring(MaxSourceUnits+1,L' '), Error::Limit));
    std::wstring deep=L"HKCR {";
    for (size_t i=0;i<MaxNesting+2;++i) deep+=L" A {";
    for (size_t i=0;i<MaxNesting+2;++i) deep+=L" }";
    deep+=L" }";
    RGS_CHECK(Parse(deep,d)==Error::Limit && d.keys.size()==1);
    std::wstring many=L"HKCR {";
    for (size_t i=0;i<MaxKeys+1;++i)
        many+=L" K"+std::to_wstring(i)+L" = s 'x'";
    many+=L" }";
    RGS_CHECK(Parse(many,d)==Error::Limit && d.keys.size()==1);
    std::wstring expansion=L"unchanged";
    RGS_CHECK(ExpandModule(L"%BOGUS%",L"C:\\module.dll",expansion)==Error::Unsupported && expansion==L"unchanged");
    RGS_CHECK(ExpandModule(L"@%MODULE%,-100",L"C:\\module.dll",expansion)==Error::Ok && expansion==L"@C:\\module.dll,-100");
    return true;
}
} // namespace

bool RunRgsParserTests() { return Originals() && RejectMalformed(); }
#ifdef OPENATL_RGS_TEST_STANDALONE
int main() { return RunRgsParserTests() ? 0 : 1; }
#endif
