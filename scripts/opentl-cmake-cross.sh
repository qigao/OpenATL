#!/usr/bin/env bash
# Run after scripts/wtl10-cross-host.sh on Linux/macOS.
set -euo pipefail
CXX="$(find "$PWD/tools/wtl10-host/llvm" -name x86_64-w64-mingw32-clang++ -print -quit)"
test -x "$CXX"
BIN="$(dirname "$CXX")"
WTL_HEADER="$(find "$PWD/tools/wtl10-host/wtl10" -type f -iname atlapp.h -print -quit)"
test -s "$WTL_HEADER"
WTL_INC="$(dirname "$WTL_HEADER")"
ATL_INC="$PWD/tools/wtl10-host/openatl/include"
test -f "$ATL_INC/atlbase.h"
GENERATOR="Unix Makefiles"
if command -v ninja >/dev/null 2>&1; then GENERATOR="Ninja"; fi
for arch in x86_64 aarch64; do
  dir="build-opentl-cmake-$arch"
  cmake -S . -B "$dir" -G "$GENERATOR" \
    -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchains/llvm-mingw.cmake" \
    "-DOPENTL_LLVM_MINGW_BIN=$BIN" \
    "-DOPENTL_TARGET_ARCH=$arch" \
    "-DOPENTL_ATL_INCLUDE_DIR=$ATL_INC" \
    "-DOPENTL_WTL_INCLUDE_DIR=$WTL_INC" \
    -DOPENTL_BUILD_QUALIFICATION=ON \
    -DOPENTL_TEST_PATCHES_APPLIED=ON
  cmake --build "$dir" --parallel 2
  python3 tests/wtl10/verify_pe_machine.py "$dir/opentl_gui_runtime.exe" \
    "$arch" --require-resources
  for base in opentl_com_p0 opentl_com_p1 opentl_com_p1b \
              opentl_registry_sandbox opentl_registrar; do
    python3 tests/wtl10/verify_pe_machine.py "$dir/$base.exe" "$arch"
  done
done
echo "OpenTL CMake OpenTL::ATL/WTL qualification build Windows x64/ARM64 PE PASS (cross-only)"
