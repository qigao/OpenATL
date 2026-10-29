#!/usr/bin/env python3
"""No second WTL source; preserve full WTL official sample test gates."""
import json
from pathlib import Path
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
def wfOriginalArm64GateMissing(text):
    for marker in (
        "openatl-llvm-arm64-original",
        "openatl-msvc-arm64-original",
        "Native ARM64 WTL10 full-original llvm-mingw CTest equivalents x5",
        "Native ARM64 WTL10 full-original MSVC CTest equivalents x5",
        "OPENATL_WTL_ORIGINAL_ARM64_LLVM_BUILD_OK",
        "OPENATL_WTL_ORIGINAL_ARM64_MSVC_BUILD_OK",
    ):
        if marker not in text:
            return True
    return False
SHA="086a6cf6a49a4318a8c519136ba6019ded7aa7f2c1d85f78c30b21183654537b3428a400a64fcdacba3a7a10a9ef05137b6f2119f59594da300d55f9ebfb1309"
CACHE="8359cdbdf25c1f58cfc7230cc6318dc2fdb804fb"
manifest=json.loads((ROOT/"vcpkg.json").read_text("utf-8"))
cmake=(ROOT/"cmake/OpenATLWTL.cmake").read_text("utf-8")
adapter_path=ROOT/"cmake/wtl/PrepareWtl.cmake"
workflow=(ROOT/".github/workflows/openatl-qualification.yml").read_text("utf-8")
tracked=set(subprocess.check_output(["git","-C",str(ROOT),"ls-files","-z"]).decode().split("\0"))
fail=[]
if "third_party/wtl/WTL10_10320_Final.zip" in tracked or (ROOT/"third_party/wtl/WTL10_10320_Final.zip").exists():
    fail.append("repository WTL ZIP duplicated vcpkg source")
if manifest.get("dependencies") != ["wtl"] or manifest.get("overrides") != [{"name":"wtl","version":"10.0.10320","port-version":7}]:
    fail.append("WTL vcpkg manifest/version is not exact")
for token in (SHA, "openatl-wtl-source.txt", "provider=vcpkg-cache/ports/wtl",
              "Samples/GuidGen/GuidGen.cpp", "AppWizard/Setup.js",
              'target_include_directories(openatl_atl INTERFACE "${_wtl_source}")'):
    if token not in cmake: fail.append("WTL source gate missing: "+token)
for token in ("FetchContent", "third_party/wtl", "downloads.sourceforge.net", "file(DOWNLOAD"):
    if token in cmake: fail.append("Forbidden alternate WTL provider: "+token)
if ('option(OPENATL_WTL_FULL_ORIGINAL' not in cmake or
        'if(NOT OPENATL_WTL_FULL_ORIGINAL)' not in cmake or
        "Original vcpkg WTL is mandatory" not in cmake or
        "The full original WTL source admission is qualification-only" in
             (ROOT/"CMakeLists.txt").read_text("utf-8")):
    fail.append("only original installed vcpkg WTL may be configured in consumer mode")
if adapter_path.exists() or "cmake/wtl/PrepareWtl.cmake" in tracked:
    fail.append("legacy build-local WTL adapter must be completely deleted")
for token in ("file(COPY", "file(REMOVE_RECURSE", "PrepareWtl.cmake",
              "wtl/adapter.sha256", "file(DOWNLOAD", "FetchContent"):
    if token in cmake:
        fail.append("original WTL provider or build-local copy violation: "+token)
for required in ("OPENATL_WTL_FULL_ORIGINAL_WIN32_OK",
                 "OPENATL_WTL_FULL_ORIGINAL_LLVM_I686_OK",
                 "OPENATL_WTL_FULL_ORIGINAL_MSVC_X64_OK",
                 "OPENATL_WTL_FULL_ORIGINAL_LLVM_X64_OK",
                 "openatl-msvc-arm64-original", "openatl-llvm-arm64-original",
                 "Native ARM64 WTL10 full-original MSVC CTest equivalents x5",
                 "Native ARM64 WTL10 full-original llvm-mingw CTest equivalents x5"):
    if required not in workflow:
        fail.append("original-only WTL compiler/native ARM64 gate missing: "+required)
# Every builder and native artifact consumer must use the actual PR HEAD,
# never GitHub's synthetic pull_request merge SHA.
exact_ref = "ref: ${{ github.event.pull_request.head.sha || github.sha }}"
if (workflow.count(exact_ref) != 7 or
        "OPENATL_EXPECTED_CHECKOUT_SHA: ${{ github.event.pull_request.head.sha || github.sha }}" not in workflow or
        "current_checkout_sha" not in (ROOT / "tests/wtl10/manifest_contract.py").read_text("utf-8")):
    fail.append("every CI checkout and native ARM64 consumer must bind exact PR HEAD")
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
original_manifest=ROOT/"tests/wtl10/original_arm64_manifest.py"
if (not original_manifest.is_file()
        or 'OPENATL_WTL_FULL_ORIGINAL:BOOL=ON' not in original_manifest.read_text("utf-8")
        or 'OPENATL_WTL_ORIGINAL_ARM64_PE_OK' not in original_manifest.read_text("utf-8")
        or wfOriginalArm64GateMissing(workflow)):
    fail.append("independent original ARM64 PE/SHA artifacts and 5x native behavior required")
direct = (ROOT/"tests/wtl_unmodified/direct_headers.cpp.in").read_text("utf-8")
if ("OriginalPrintDialogCallbackNoCom" not in direct or
        "IPrintDialogCallback" not in direct or
        "IObjectWithSite" not in direct):
    fail.append("real original WTL PrintDlgEx/site behavior must be verified")
if ("server_monitor.cpp" not in (ROOT/"tests/wtl_unmodified/CMakeLists.txt").read_text("utf-8")
        or "RunServerMonitorTests()" not in (ROOT/"tests/wtl_unmodified/direct_headers.cpp.in").read_text("utf-8")):
    fail.append("original WTL server monitor bounded worker join must remain native test")
monitor_src=(ROOT/"tests/wtl_unmodified/server_monitor.cpp").read_text("utf-8")
if ("module.StartMonitor()" not in monitor_src
        or "::ExitProcess(status)" not in monitor_src
        or "--start-monitor-process" not in native_script
        or "openatl.wtl_original_start_monitor_process" not in
            (ROOT/"tests/wtl_unmodified/CMakeLists.txt").read_text("utf-8")):
    fail.append("original detached StartMonitor requires process-only native test; no join claim")
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
      "SHA512=pinned adapter=absent original-only=1 feature-parity=UNQUALIFIED")
