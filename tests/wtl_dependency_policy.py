#!/usr/bin/env python3
"""No second WTL source; preserve full WTL official sample test gates."""
import json
from pathlib import Path
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
SHA="086a6cf6a49a4318a8c519136ba6019ded7aa7f2c1d85f78c30b21183654537b3428a400a64fcdacba3a7a10a9ef05137b6f2119f59594da300d55f9ebfb1309"
CACHE="8359cdbdf25c1f58cfc7230cc6318dc2fdb804fb"
manifest=json.loads((ROOT/"vcpkg.json").read_text("utf-8"))
cmake=(ROOT/"cmake/OpenATLWTL.cmake").read_text("utf-8")
adapter=(ROOT/"cmake/wtl/PrepareWtl.cmake").read_text("utf-8")
workflow=(ROOT/".github/workflows/openatl-qualification.yml").read_text("utf-8")
tracked=set(subprocess.check_output(["git","-C",str(ROOT),"ls-files","-z"]).decode().split("\0"))
fail=[]
if "third_party/wtl/WTL10_10320_Final.zip" in tracked or (ROOT/"third_party/wtl/WTL10_10320_Final.zip").exists():
    fail.append("repository WTL ZIP duplicated vcpkg source")
if manifest.get("dependencies") != ["wtl"] or manifest.get("overrides") != [{"name":"wtl","version":"10.0.10320","port-version":7}]:
    fail.append("WTL vcpkg manifest/version is not exact")
for token in (SHA, "openatl-wtl-source.txt", "provider=vcpkg-cache/ports/wtl",
              "Samples/GuidGen/GuidGen.cpp", "AppWizard/Setup.js", "file(COPY"):
    if token not in cmake: fail.append("WTL source gate missing: "+token)
for token in ("FetchContent", "third_party/wtl", "downloads.sourceforge.net", "file(DOWNLOAD"):
    if token in cmake: fail.append("Forbidden alternate WTL provider: "+token)
if "function(_openatl_replace_once" not in adapter or "bug329 patch missing" not in adapter:
    fail.append("remaining compiler-compatibility adapter/port-patch contract missing")
for required in ("WTL #7 original list-sort bound patch not installed",
                 "WTL #7 original Clang ribbon template patch missing",
                 "WTL #7 original const DECIMAL patch missing",
                 "WTL #7 original command-bar bound patch not installed"):
    if required not in adapter:
        fail.append("vcpkg WTL #7 Clang source patch must not be reapplied in OpenATL")
if "function(_openatl_replace_between" in adapter:
    fail.append("feature-deleting WTL block-replacement helper must be retired")
for source in ("class CAppModule : public ATL::CComModule",
               "class CServerAppModule : public CAppModule",
               "WTL original application/COM server ABI missing"):
    if source not in adapter:
        fail.append("original WTL application/COM server class requirement missing: "+source)
cmake_source=(ROOT/"cmake/OpenATLWTL.cmake").read_text("utf-8")
if ('file(SHA256 "${_wtl_headers}/atlapp.h"' not in cmake_source or
        "WTL original CAppModule/CServerAppModule source identity mismatch" not in cmake_source):
    fail.append("build-local atlapp.h must be byte-identical to vcpkg original")
atlwin_src=(ROOT/"include/atlwin.h").read_text("utf-8")
borrow_src=(ROOT/"tests/wtl_unmodified/direct_headers.cpp.in").read_text("utf-8")
if ("operator T() const" in atlwin_src or
        "template<class TBase> class CButtonT;" in atlwin_src or
        "WTL #7 installed Clang HWND borrow constructor/assignment missing" not in adapter or
        "OriginalWtlBorrowConversion()" not in borrow_src or
        "assignedButton = borrowed;" not in borrow_src or
        "assignedEdit = borrowed;" not in borrow_src or
        "combo = borrowed;" not in borrow_src or
        "view = borrowed;" not in borrow_src):
    fail.append("WTL #7 direct Button/Edit borrow and HWND assignment must avoid ATL conversion ambiguities")
atlbase_src=(ROOT/"include/atlbase.h").read_text("utf-8")
if ('inline HRESULT WINAPI GetThemePartSize(' not in atlbase_src or
        'input ? &copied : nullptr' not in atlbase_src):
    fail.append("const RECT Win32/llvm-mingw ABI overload required without rewriting WTL")
if ('option(OPENATL_WTL_FULL_ORIGINAL' not in cmake_source
        or 'target_include_directories(openatl_atl INTERFACE "${_wtl_source}")' not in cmake_source
        or 'if(OPENATL_WTL_FULL_ORIGINAL)' not in cmake_source
        or "The full original WTL source admission is qualification-only" not in
             (ROOT/"CMakeLists.txt").read_text("utf-8")
        or "OPENATL_WTL_FULL_ORIGINAL_WIN32_OK" not in workflow
        or "OPENATL_WTL_FULL_ORIGINAL_LLVM_I686_OK" not in workflow
        or "OPENATL_WTL_FULL_ORIGINAL_MSVC_X64_OK" not in workflow
        or "OPENATL_WTL_FULL_ORIGINAL_LLVM_X64_OK" not in workflow):
    fail.append("full original WTL native MSVC/LLVM x86 and x64 gates must remain strict")
# Feature deletions must not reappear in the build-local WTL include copy.
# Original CAppModule/Server are now retained byte-identically, but
# concurrent original StartMonitor remains unqualified until owner/join proof.
for forbidden in ("legacy hyperlink registry color access",
                  "registry-only CRegProperty module",
                  '"COM callback-based PrintDlgEx")'):
    if forbidden in adapter:
        fail.append("feature-deleting WTL adapter remains: "+forbidden)
for required in ("WTL original hyperlink color query missing",
                 "WTL original PrintDlgEx callback missing",
                 "WTL original registry property map missing"):
    if required not in adapter:
        fail.append("original WTL public API restoration removed: "+required)
if workflow.count(CACHE) != 4 or workflow.count("setup-vcpkg-curl-cache.py") != 4:
    fail.append("WTL vcpkg setup must cover all four native/cross CI build jobs")
if "wtltest-ui-only.patch" in workflow or "--preset" not in workflow:
    fail.append("official unchanged samples/native tests must remain configured")
if (workflow.count("-DOPENATL_BUILD_WTL_UNMODIFIED=ON") < 7
        or workflow.count("openatl_wtl_unmodified_headers") < 2):
    fail.append("original WTL build must run on all CI compiler/RID profiles")
native_script=(ROOT/"tests/wtl10/native_arm64.py").read_text("utf-8")
if ('"openatl_wtl_unmodified_headers"' not in native_script or
        "OPENATL_WTL_ORIGINAL_MAP_OK" not in native_script):
    fail.append("native ARM64 must execute original WTL class factory/cookie contract")
direct = (ROOT/"tests/wtl_unmodified/direct_headers.cpp.in").read_text("utf-8")
if ("OriginalPrintDialogCallbackNoCom" not in direct or
        "IPrintDialogCallback" not in direct or
        "IObjectWithSite" not in direct):
    fail.append("real original WTL PrintDlgEx/site behavior must be verified")
if ("server_monitor.cpp" not in (ROOT/"tests/wtl_unmodified/CMakeLists.txt").read_text("utf-8")
        or "RunServerMonitorTests()" not in (ROOT/"tests/wtl_unmodified/direct_headers.cpp.in").read_text("utf-8")):
    fail.append("original WTL server monitor bounded worker join must remain native test")
if ("reg_property.cpp" not in (ROOT/"tests/wtl_unmodified/CMakeLists.txt").read_text("utf-8")
        or "RunRegPropertyTests()" not in (ROOT/"tests/wtl_unmodified/direct_headers.cpp.in").read_text("utf-8")):
    fail.append("original CRegProperty real HKCU native behavior must remain qualified")
audit_script=(ROOT/"tests/wtl_unmodified/audit_wtl_api.py")
if (not audit_script.is_file() or
        "WTL_REGISTRY_API_INVENTORY_OK" not in audit_script.read_text("utf-8") or
        "python tests/wtl_unmodified/audit_wtl_api.py" not in workflow):
    fail.append("pinned original WTL registry API inventory must remain mandatory")
if ("WTL_REGISTRY_MISSING_CONTRACT_OK" not in audit_script.read_text("utf-8") or
        "ERROR_FILE_NOT_FOUND" not in (ROOT/"tests/wtl_unmodified/reg_property.cpp").read_text("utf-8")):
    fail.append("original WTL optional missing and precise LSTATUS tests required")
expanded=(ROOT/"tests/wtl_unmodified/reg_property.cpp").read_text("utf-8")
if ("WTL_REGISTRY_EXTENDED_CONTRACT_OK" not in audit_script.read_text("utf-8")
        or "BEGIN_REGPROP_MAP(MapProperties)" not in expanded
        or "WTL::CRegProperty::BinaryProp" not in expanded
        or "WTL::CRegProperty::CharArrayProp" not in expanded
        or "ERROR_OUTOFMEMORY" not in expanded
        or "ScopedFont" not in expanded):
    fail.append("original WTL CRTP/binary/font/array native property gates required")
link_src=(ROOT/"tests/wtl_unmodified/hyperlink_registry.cpp")
direct_src=(ROOT/"tests/wtl_unmodified/direct_headers.cpp.in").read_text("utf-8")
cmake_src=(ROOT/"tests/wtl_unmodified/CMakeLists.txt").read_text("utf-8")
if (not link_src.is_file()
        or "RegOverridePredefKey" not in link_src.read_text("utf-8")
        or "RunOriginalHyperlinkRegistryTests()" not in direct_src
        or "openatl.wtl_original_hyperlink_hkcu" not in cmake_src
        or '"--hyperlink"' not in native_script):
    fail.append("original WTL hyperlink HKCU isolated native behavior is mandatory")
if fail:
    for item in fail: print("WTL_VCPKG_POLICY_FAIL "+item,file=sys.stderr)
    raise SystemExit(1)
print("WTL_VCPKG_POLICY_OK version=10.0.10320#7 single-source=1 "
      "SHA512=pinned adapter-feature-parity=UNQUALIFIED")
