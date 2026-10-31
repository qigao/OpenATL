# Windows Unicode and code-page boundaries

`OpenWTL::WTL` uses **Windows NLS only**, with no `Salts::Unicode`, Unicode
data tables, optional Unicode SDK, or separate provider. `CString` and
`CComBSTR` remain UTF-16 owned Win32/COM strings; conversion does not
implicitly initialize COM, normalize, case-fold or change ACP.

## Strict counted conversions

Include `openatl/text_encoding.h` or the compatibility include
`openatl/unicode.h`. All operations return `HRESULT` and preserve
the prior owned output on failure.

| API | Windows implementation |
| --- | --- |
| `Utf8ToUtf16` | `MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS)` |
| `Utf16ToUtf8` | `WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS)` |
| `AnsiToUtf16`, `Utf16ToAnsi` | Resolve `GetACP()`, apply strict conversion flags |
| `CodePageToUtf16`, `Utf16ToCodePage` | Explicit Windows code page, counted input |
| `ValidateUtf16` | Check surrogate pairs before encoding |

Embedded NULs and successful empty input are preserved. Malformed UTF-8,
unpaired UTF-16 surrogates, and lossy/best-fit/default ANSI conversions
return `HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION)`. Unsupported code
pages/strict flags **fail closed**, without a permissive fallback.
`INT_MAX` counts, arithmetic overflow and allocation failure are bounded.

`CString`, `CComBSTR`, and `CConversionScope` retain explicit UTF-8 and
ACP conversions, Unicode string ownership, lexical scope, and
error propagation; a failed conversion never replaces the previous
string or BSTR owner. Win32 active-code-page manifests independently
exercise UTF-8 ACP behavior.

## ATL conversion objects

`atlconv.h` supports `CA2WEX<N>`, `CW2AEX<N>`, `CA2W`, `CW2A`, and the
Unicode aliases `CA2T` / `CT2A` and `CA2TEX<N>` / `CT2AEX<N>`.
These objects do not require `USES_CONVERSION`:

```cpp
ATL::CW2A bytes(L"text", CP_UTF8);
ATL::CA2W text(bytes, CP_UTF8);
```

The one-argument constructors select `CP_THREAD_ACP`; the two-argument
constructors select an explicit Windows code page. `m_psz` and
`m_szBuffer[N]` are public, with mutable pointer conversion. Results including
the NUL terminator that fit in N elements use the inline buffer; larger
results use object-owned heap storage. The objects cannot be copied, and a
borrowed output pointer must not outlive its conversion object. Null input
produces a null pointer; empty input produces a nonnull empty buffer.

The existing strict encoding policy still rejects malformed UTF-8/UTF-16
and lossy/default ANSI substitutions. Failed conversions throw
`CAtlException` with the failing HRESULT (or terminate under
`_ATL_NO_EXCEPTIONS`). This policy is stricter than permissive Microsoft ATL
ANSI conversions; complete conversion-class compatibility is not claimed.
Other conversion classes and aliases remain future work.

Legacy `A2W` / `W2A` macros retain `USES_CONVERSION` lexical ownership,
process ACP, and their existing null-pointer / `GetLastError` failure contract.
`CW2A` is now a conversion type, so save a named object when retaining its
output pointer instead of retaining a pointer from a temporary expression.

## Deliberately unsupported advanced text operations

The old opt-in `OPENATL_USE_SALTS_UNICODE` / `Salts::Unicode` processing
module is removed. `NormalizeNfcUtf8/Utf16`, `CaseFoldUtf8/Utf16`, and
`TrimUtf8` were **only** available with that optional SDK and are no longer
exported. Windows `LCMapStringEx` lowercase mapping is not Unicode
full/default case folding; no API claims to be equivalent to it. If such
semantics are needed, they must be designed separately and correctly,
not replaced with incomplete fallbacks.

Passing `-DOPENATL_USE_SALTS_UNICODE` now fails during CMake configuration,
rather than silently selecting or downloading another Unicode engine.

## Native qualification

`openatl.unicode` checks the host ACP; `openatl.unicode_utf8_acp`
uses an embedded UTF-8 active-code-page manifest. Both run with the same
Windows x64/ARM64, MSVC/llvm-mingw native and real-ARM64 x5 acceptance.
These tests verify Win32 conversion behavior, including malformed and lossy
input, embedded NULs, ownership, and the absence of implicit COM
initialization. The UTF-8 ACP manifest requires Windows 10 1903 or newer.

Windows API references:
- https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-multibytetowidechar
- https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-widechartomultibyte
- https://learn.microsoft.com/en-us/windows/apps/design/globalizing/use-utf8-code-page
