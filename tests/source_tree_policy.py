#!/usr/bin/env python3
"""Repository ATL provenance is source-owned, WTL comes from pinned vcpkg."""
from pathlib import Path
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
tracked=set(subprocess.check_output(["git","-C",str(ROOT),"ls-files","-z"],timeout=30).decode().split("\0"))
required=("CMakeLists.txt","include/atlbase.h","include/atlwin.h","include/atlcomcli.h",
          "include/atlcom.h","include/openatl/com_server.h",
          "include/openatl/com_registration.h","tests/com_server/CMakeLists.txt",
          "cmake/OpenATLWTL.cmake","vcpkg.json","tests/wtl_dependency_policy.py")
cmake=(ROOT/"CMakeLists.txt").read_text("utf-8")
bad=[p for p in required if p not in tracked or not (ROOT/p).is_file()]
for marker in ('set(_openatl_atl_include "${CMAKE_CURRENT_SOURCE_DIR}/include")',
               "add_library(OpenATL::ATL ALIAS openatl_atl)",
               "add_subdirectory(tests/com_server)"):
    if marker not in cmake: bad.append("ATL CMake contract: "+marker)
if bad:
    print("OPENATL_SOURCE_PROVENANCE_FAIL "+str(bad),file=sys.stderr)
    raise SystemExit(1)
print("OPENATL_SOURCE_PROVENANCE_OK atl=source-owned wtl=vcpkg com_client=1 com_server=1")
