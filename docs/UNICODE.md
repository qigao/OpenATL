# Unicode and Windows encoding boundaries

All consumers link `OpenATL::ATL`. Win32/WTL strings and COM BSTRs retain
their UTF-16 representation and code-unit indexing. Narrow constructors and
`A2W`/`W2A` retain system ACP semantics; UTF-8 is explicit. No implicit
normalization, case folding, BOM removal, locale change or COM initialization
occurs.

## Counted conversions

Include `openatl/text_encoding.h` or `openatl/unicode.h`. Functions in
`OpenATL::Unicode` return `HRESULT`:

| Function | Input and output |
| --- | --- |
| `Utf8ToUtf16` | `std::string_view` to owned `std::wstring` |
| `Utf16ToUtf8` | `std::wstring_view` to owned `std::string` |
| `AnsiToUtf16` / `Utf16ToAnsi` | The same contract, using `GetACP()` |
| `CodePageToUtf16` / `Utf16ToCodePage` | Explicit Windows code page, counted input and owned output |
| `ValidateUtf16` | Validate surrogate pairing without allocating |

Input is borrowed and immutable during the call. Output is replaced only on
success; embedded NULs are preserved and empty input produces empty output.
Malformed UTF-8, unpaired UTF-16 surrogates and lossy/default/best-fit ANSI
conversion return `HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION)`. Windows
must support the strict conversion flags for a selected code page; unsupported
pages fail without retrying permissively. Counted NLS lengths are limited to
`INT_MAX`; arithmetic/length overflow and allocation failure are distinct errors.

`CString` and `CComBSTR` expose checked `AssignAnsi(std::string_view)`,
`AssignUtf8(std::string_view)` and `ToUtf8(std::string&)`. Failed assignments
retain the old value and BSTR owner. Existing NUL-terminated narrow constructors
now use this strict ACP decoder: invalid input/allocation failure throws
`CAtlException`. Under `_ATL_NO_EXCEPTIONS`, constructor failure terminates;
use checked assignment to handle errors in that mode. Null narrow constructors
retain empty/null behavior. Wide constructors still store supplied code units;
encoding conversions validate them at the boundary.

`CConversionScope::ToWideUtf8` and `ToUtf8` add explicit UTF-8 methods beside
the ACP methods/macros. They accept NUL-terminated strings and return null on
failure with a Windows last-error code; null input returns null. Each result
remains owned by its lexical scope and valid across subsequent conversions.
Use counted, owned conversions for embedded NULs or storage beyond that scope.

## Optional Salts Unicode 17 processing

The default conversion path uses Windows NLS with no Salts dependency.
`OPENATL_USE_SALTS_UNICODE=ON` enables explicit NFC, full default case folding
and Unicode White_Space trimming through `Salts::Unicode`. `SALTS_ROOT` must
select a complete installed Salts 2.3 SDK matching the target/compiler/runtime.
Missing targets, missing SDKs and cached packages outside this root fail
configuration; there is no automatic download or source fallback.
`OpenATL::ATL` carries the dependency and feature definition.

Include `openatl/unicode.h`. Enabled functions are `NormalizeNfcUtf8`,
`NormalizeNfcUtf16`, `CaseFoldUtf8`, `CaseFoldUtf16` and `TrimUtf8`. Each takes
counted input, an owned output string and an optional input-byte quota
(default 1 MiB). UTF-16 transforms enforce the quota on the UTF-8 working
representation. Input/output aliasing is supported; failures preserve output.
When disabled, advanced functions are absent rather than replaced by a weaker
engine. Unicode data is fixed to 17.0.0 and checked against public SDK macros.
Folding and normalization remain separate from each other and existing
CString/BSTR comparisons. Salts' other scalar/segmentation APIs remain available
from its public headers; no Unicode tables are duplicated here.

NFC allocates at most four `uint32_t` workspace elements and sixteen output
bytes per input byte, plus a terminator. Arithmetic/allocation errors preserve
output. This byte quota bounds memory, not latency: Salts documents quadratic
worst-case NFC reordering in long combining sequences. Products processing
untrusted text should select an appropriate quota and scheduling policy.

The qualified optional profile consumes published
`Salts.Native.2.3.0-rc.17`, SHA256
`dfda2bb9fbbbbf8b13f8ba941d3b4eb302814fd90da5869c6b22111f322e68c3`,
using its Windows x64 MSVC SDK and dynamic CRT
(`CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL`). Put the SDK `bin` on runtime
`PATH`; no runtime DLLs are copied. Other compiler/architecture combinations
require matching SDKs and their own adapter qualification. Salts retains its
own license and Unicode data notices. Linkage includes the static Unicode
library and its public `Salts::Core` dependency; no size/performance improvement
is claimed.

## Native qualification

`openatl.unicode` tests the host ACP; `openatl.unicode_utf8_acp` embeds a UTF-8
active-code-page manifest to test constructors under UTF-8 ACP. This manifest
qualification requires Windows 10 1903 or newer; basic conversions retain the
project's Windows API baseline. Both tests run natively on MSVC/LLVM-MinGW
x64/ARM64. The published-SDK MSVC profile additionally tests NFC, case folding,
whitespace, aliasing, malformed input and quotas. Cross builds verify PE and
resources, not runtime encoding behavior.

References:
- https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-multibytetowidechar
- https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-widechartomultibyte
- https://learn.microsoft.com/en-us/windows/apps/design/globalizing/use-utf8-code-page
- https://github.com/qigao/salts/releases/tag/v2.3.0-rc.17
