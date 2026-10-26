#!/usr/bin/env bash
# RATL = isolated conformance reference, never a fallback WTL implementation.
# Windows execution is required in the separate native WTL10 workflow.
set -euo pipefail
BIN="$(dirname "$(find "$PWD/tools/wtl10-host/llvm" -name x86_64-w64-mingw32-clang++ -print -quit)")"
test -d "$BIN"
OPENATL="$PWD/tools/wtl10-host/openatl"
RATL="$PWD/tools/wtl10-host/ratl-com"
OPENPIN=7b587f3e10f3f8d93a01522033de80007368bad5
RATLPIN=2e2efa182d7c6ee9037454d23780991fb8543084
test "$(git -C "$OPENATL" rev-parse HEAD)" = "$OPENPIN"
git clone --quiet https://github.com/katahiromz/RATL.git "$RATL"
git -C "$RATL" checkout --quiet --detach "$RATLPIN"
test "$(git -C "$RATL" rev-parse HEAD)" = "$RATLPIN"
git -C "$RATL" diff --quiet HEAD -- include/atl/statreg.h
test -f "$RATL/include/atl/atlcom.h"
test -f "$OPENATL/include/atlcom.h"

for cpu in x86_64 aarch64; do
  CXX="$BIN/$cpu-w64-mingw32-clang++"
  test -x "$CXX"
  OPENEXE="build-wtl10-host/atl_com_openatl_$cpu.exe"
  RATLEXE="build-wtl10-host/atl_com_ratl_$cpu.exe"
  "$CXX" -std=c++17 -O0 -DNDEBUG -DUNICODE -D_UNICODE \
    -D_ATL_APARTMENT_THREADED -D_WIN32_WINNT=0x0601 \
    -DATL_CONFORMANCE_OPENATL=1 "-I$OPENATL/include" \
    tests/atl_conformance/com_p0.cpp -o "$OPENEXE" \
    -static -lole32 -loleaut32 -luuid -lrpcrt4 -lshlwapi \
    -ladvapi32 -lgdi32 -luser32 -lkernel32
  python3 tests/wtl10/verify_pe_machine.py "$OPENEXE" "$cpu"
  if [ "$cpu" = x86_64 ]; then
    "$CXX" -std=c++17 -O0 -DNDEBUG -DUNICODE -D_UNICODE \
      -D_ATL_APARTMENT_THREADED -D_WIN32_WINNT=0x0601 \
      -DATL_CONFORMANCE_RATL=1 -D__RATL__ -DINITGUID -D_USE_PSEH3 \
      -D_CRT_NON_CONFORMING_SWPRINTFS "-I$RATL/include/atl" "-I$RATL/compat" \
      tests/atl_conformance/com_p0.cpp -o "$RATLEXE" \
      -static -lole32 -loleaut32 -luuid -lrpcrt4 -lshlwapi \
      -ladvapi32 -lgdi32 -luser32 -lkernel32
    python3 tests/wtl10/verify_pe_machine.py "$RATLEXE" "$cpu"
    RATLP1="build-wtl10-host/atl_com_p1_ratl_x86_64.exe"
    "$CXX" -std=c++17 -O0 -DNDEBUG -DUNICODE -D_UNICODE \
      -D_ATL_APARTMENT_THREADED -D_WIN32_WINNT=0x0601 \
      -D__RATL__ -DINITGUID -D_USE_PSEH3 -D_CRT_NON_CONFORMING_SWPRINTFS \
      "-I$RATL/include/atl" "-I$RATL/compat" \
      tests/atl_conformance/ratl_p1_reference.cpp -o "$RATLP1" \
      -static -lole32 -loleaut32 -luuid -lrpcrt4 -lshlwapi \
      -ladvapi32 -lgdi32 -luser32 -lkernel32
    python3 tests/wtl10/verify_pe_machine.py "$RATLP1" x86_64
    RATL_REG="build-wtl10-host/atl_ratl_registry_ref_x86_64.exe"
    "$CXX" -std=c++17 -O0 -DNDEBUG -DUNICODE -D_UNICODE \
      -D_ATL_APARTMENT_THREADED -D_WIN32_WINNT=0x0601 \
      -D__RATL__ -DINITGUID -D_USE_PSEH3 -D_CRT_NON_CONFORMING_SWPRINTFS \
      "-I$RATL/include/atl" "-I$RATL/compat" \
      tests/atl_conformance/ratl_registry_reference.cpp -o "$RATL_REG" \
      -static -lole32 -loleaut32 -luuid -lrpcrt4 -lshlwapi \
      -ladvapi32 -lgdi32 -luser32 -lkernel32
    python3 tests/wtl10/verify_pe_machine.py "$RATL_REG" x86_64
    RATL_NESTED_BASE="build-wtl10-host/ratl_nested_original_x64.exe"
    "$CXX" -std=c++17 -O0 -DNDEBUG -DUNICODE -D_UNICODE \
      -D_ATL_APARTMENT_THREADED -D_WIN32_WINNT=0x0601 \
      -D__RATL__ -DINITGUID -D_USE_PSEH3 -D_CRT_NON_CONFORMING_SWPRINTFS \
      "-I$RATL/include/atl" "-I$RATL/compat" \
      tests/atl_conformance/ratl_rgs_nested.cpp -o "$RATL_NESTED_BASE" \
      -static -lole32 -loleaut32 -luuid -lrpcrt4 -lshlwapi \
      -ladvapi32 -lgdi32 -luser32 -lkernel32
    python3 tests/wtl10/verify_pe_machine.py "$RATL_NESTED_BASE" x86_64

    RATL_PATCHED="$PWD/tools/wtl10-host/ratl-p1d-fixed"
    git clone --quiet "$RATL" "$RATL_PATCHED"
    git -C "$RATL_PATCHED" checkout --quiet --detach "$RATLPIN"
    test "$(git -C "$RATL_PATCHED" rev-parse HEAD)" = "$RATLPIN"
    python3 scripts/ratl-rgs-nested-test-patch.py "$RATL_PATCHED"
    RATL_NESTED_FIXED="build-wtl10-host/ratl_nested_patched_x64.exe"
    "$CXX" -std=c++17 -O0 -DNDEBUG -DUNICODE -D_UNICODE \
      -D_ATL_APARTMENT_THREADED -D_WIN32_WINNT=0x0601 \
      -D__RATL__ -DINITGUID -D_USE_PSEH3 -D_CRT_NON_CONFORMING_SWPRINTFS \
      "-I$RATL_PATCHED/include/atl" "-I$RATL_PATCHED/compat" \
      tests/atl_conformance/ratl_rgs_nested.cpp -o "$RATL_NESTED_FIXED" \
      -static -lole32 -loleaut32 -luuid -lrpcrt4 -lshlwapi \
      -ladvapi32 -lgdi32 -luser32 -lkernel32
    python3 tests/wtl10/verify_pe_machine.py "$RATL_NESTED_FIXED" x86_64
    RATL_DENIED="build-wtl10-host/ratl_nested_injected_x64.exe"
    "$CXX" -std=c++17 -O0 -DNDEBUG -DUNICODE -D_UNICODE \
      -D_ATL_APARTMENT_THREADED -D_WIN32_WINNT=0x0601 \
      -DRATL_P1D_TEST_DENY_DELETE=1 \
      -D__RATL__ -DINITGUID -D_USE_PSEH3 -D_CRT_NON_CONFORMING_SWPRINTFS \
      "-I$RATL_PATCHED/include/atl" "-I$RATL_PATCHED/compat" \
      tests/atl_conformance/ratl_rgs_nested.cpp -o "$RATL_DENIED" \
      -static -lole32 -loleaut32 -luuid -lrpcrt4 -lshlwapi \
      -ladvapi32 -lgdi32 -luser32 -lkernel32
    python3 tests/wtl10/verify_pe_machine.py "$RATL_DENIED" x86_64
  fi
  # P1 is OpenATL-only; RATL remains an isolated x64 P0 reference.
  P1_EXE="build-wtl10-host/atl_com_p1_openatl_$cpu.exe"
  "$CXX" -std=c++17 -O0 -DNDEBUG -DUNICODE -D_UNICODE \
    -D_ATL_APARTMENT_THREADED -D_WIN32_WINNT=0x0601 \
    "-I$OPENATL/include" tests/atl_conformance/com_p1.cpp -o "$P1_EXE" \
    -static -lole32 -loleaut32 -luuid -lrpcrt4 -lshlwapi \
    -ladvapi32 -lgdi32 -luser32 -lkernel32
  python3 tests/wtl10/verify_pe_machine.py "$P1_EXE" "$cpu"

  P1B_EXE="build-wtl10-host/atl_com_p1b_openatl_$cpu.exe"
  "$CXX" -std=c++17 -O0 -DNDEBUG -DUNICODE -D_UNICODE \
    -D_ATL_APARTMENT_THREADED -D_WIN32_WINNT=0x0601 \
    "-I$OPENATL/include" tests/atl_conformance/com_p1b_map.cpp \
    tests/atl_conformance/com_p1b_entry.cpp -o "$P1B_EXE" \
    -static -lole32 -loleaut32 -luuid -lrpcrt4 -lshlwapi \
    -ladvapi32 -lgdi32 -luser32 -lkernel32
  python3 tests/wtl10/verify_pe_machine.py "$P1B_EXE" "$cpu"

  SANDBOX_EXE="build-wtl10-host/atl_registry_sandbox_$cpu.exe"
  "$CXX" -std=c++17 -O0 -DNDEBUG -DUNICODE -D_UNICODE \
    -D_ATL_APARTMENT_THREADED -D_WIN32_WINNT=0x0601 \
    "-I$OPENATL/include" tests/atl_conformance/registry_sandbox.cpp \
    -o "$SANDBOX_EXE" -static -lole32 -loleaut32 -luuid \
    -lrpcrt4 -lshlwapi -ladvapi32 -lgdi32 -luser32 -lkernel32
  python3 tests/wtl10/verify_pe_machine.py "$SANDBOX_EXE" "$cpu"

  REGREAL_EXE="build-wtl10-host/atl_p1c_registrar_$cpu.exe"
  "$CXX" -std=c++17 -O0 -DNDEBUG -DUNICODE -D_UNICODE \
    -D_ATL_APARTMENT_THREADED -D_WIN32_WINNT=0x0601 \
    "-I$OPENATL/include" tests/atl_conformance/registry_real.cpp \
    tests/atl_conformance/com_p1b_entry.cpp -o "$REGREAL_EXE" \
    -static -lole32 -loleaut32 -luuid -lrpcrt4 -lshlwapi \
    -ladvapi32 -lgdi32 -luser32 -lkernel32
  python3 tests/wtl10/verify_pe_machine.py "$REGREAL_EXE" "$cpu"
done
echo "ATL COM P0/P1 separate PE cross-builds PASS (RATL x64 P0 reference; OpenATL P1 x64+ARM64)"
