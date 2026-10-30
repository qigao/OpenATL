#!/usr/bin/env python3
"""Repository WTL and ATL source provenance is wholly owned in this tree."""
from pathlib import Path
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
tracked=set(subprocess.check_output(["git","-C",str(ROOT),"ls-files","-z"],timeout=30).decode().split("\0"))
required=("CMakeLists.txt","include/atlbase.h","include/atlwin.h","include/atlcomcli.h",
          "include/atlcom.h","include/openatl/com_server.h",
          "include/openatl/com_registration.h","tests/com_server/CMakeLists.txt",
          "cmake/OpenATLWTL.cmake","wtl/SOURCE.md","wtl/source-manifest.json",
          "wtl/MS-PL.txt","wtl/Include/atlapp.h","tests/wtl_dependency_policy.py")
cmake=(ROOT/"CMakeLists.txt").read_text("utf-8")
bad=[p for p in required if p not in tracked or not (ROOT/p).is_file()]
for marker in ('set(_openatl_atl_include "${CMAKE_CURRENT_SOURCE_DIR}/include")',
               "add_library(OpenWTL::WTL ALIAS openwtl_wtl)",
               "target_link_libraries(openwtl_wtl INTERFACE openatl_atl)",
               "add_subdirectory(tests/com_server)"):
    if marker not in cmake: bad.append("ATL CMake contract: "+marker)
if bad:
    print("OPENATL_SOURCE_PROVENANCE_FAIL "+str(bad),file=sys.stderr)
    raise SystemExit(1)
print("OPENATL_SOURCE_PROVENANCE_OK atl=internal wtl=repository-owned public=OpenWTL::WTL com_client=1 com_server=1")
