#!/usr/bin/env python3
"""No second WTL source; preserve full WTL official sample test gates."""
import json
from pathlib import Path
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
SHA="086a6cf6a49a4318a8c519136ba6019ded7aa7f2c1d85f78c30b21183654537b3428a400a64fcdacba3a7a10a9ef05137b6f2119f59594da300d55f9ebfb1309"
CACHE="8ab7be066a1fa2b49522fffcf3d70c419b26a797"
manifest=json.loads((ROOT/"vcpkg.json").read_text("utf-8"))
cmake=(ROOT/"cmake/OpenATLWTL.cmake").read_text("utf-8")
adapter=(ROOT/"cmake/wtl/PrepareWtl.cmake").read_text("utf-8")
workflow=(ROOT/".github/workflows/openatl-qualification.yml").read_text("utf-8")
tracked=set(subprocess.check_output(["git","-C",str(ROOT),"ls-files","-z"]).decode().split("\0"))
fail=[]
if "third_party/wtl/WTL10_10320_Final.zip" in tracked or (ROOT/"third_party/wtl/WTL10_10320_Final.zip").exists():
    fail.append("repository WTL ZIP duplicated vcpkg source")
if manifest.get("dependencies") != ["wtl"] or manifest.get("overrides") != [{"name":"wtl","version":"10.0.10320","port-version":6}]:
    fail.append("WTL vcpkg manifest/version is not exact")
for token in (SHA, "openatl-wtl-source.txt", "provider=vcpkg-cache/ports/wtl",
              "Samples/GuidGen/GuidGen.cpp", "AppWizard/Setup.js", "file(COPY"):
    if token not in cmake: fail.append("WTL source gate missing: "+token)
for token in ("FetchContent", "third_party/wtl", "downloads.sourceforge.net", "file(DOWNLOAD"):
    if token in cmake: fail.append("Forbidden alternate WTL provider: "+token)
if "function(_openatl_replace_between" not in adapter or "bug329 patch missing" not in adapter:
    fail.append("build-local compatibility adapter/port patch contract missing")
if workflow.count(CACHE) != 4 or workflow.count("setup-vcpkg-curl-cache.py") != 4:
    fail.append("WTL vcpkg setup must cover all four native/cross CI build jobs")
if "wtltest-ui-only.patch" in workflow or "--preset" not in workflow:
    fail.append("official unchanged samples/native tests must remain configured")
if (workflow.count("-DOPENATL_BUILD_WTL_UNMODIFIED=ON") != 5
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
audit_script=(ROOT/"tests/wtl_unmodified/audit_wtl_api.py")
if (not audit_script.is_file() or
        "WTL_REGISTRY_API_INVENTORY_OK" not in audit_script.read_text("utf-8") or
        "python tests/wtl_unmodified/audit_wtl_api.py" not in workflow):
    fail.append("pinned original WTL registry API inventory must remain mandatory")
if fail:
    for item in fail: print("WTL_VCPKG_POLICY_FAIL "+item,file=sys.stderr)
    raise SystemExit(1)
print("WTL_VCPKG_POLICY_OK version=10.0.10320#6 single-source=1 "
      "SHA512=pinned adapter-feature-parity=UNQUALIFIED")
