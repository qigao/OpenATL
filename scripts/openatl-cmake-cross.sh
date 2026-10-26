#!/usr/bin/env bash
# Run after scripts/wtl10-cross-host.sh on Linux/macOS.
set -euo pipefail
CXX="$(find "$PWD/tools/wtl10-host/llvm" -name x86_64-w64-mingw32-clang++ -print -quit)"
test -x "$CXX"
BIN="$(dirname "$CXX")"
ATL_INC="$PWD/include"
test -f "$ATL_INC/atlbase.h"
GENERATOR="Unix Makefiles"
if command -v ninja >/dev/null 2>&1; then GENERATOR="Ninja"; fi
for arch in x86_64 aarch64; do
  if [ "$arch" = "x86_64" ]; then
    triplet=x64-openatl-mingw
  else
    triplet=arm64-openatl-mingw
  fi
  test -s "$PWD/vcpkg_installed/$triplet/include/atlapp.h"
  dir="build-openatl-cmake-$arch"
  cmake -S . -B "$dir" -G "$GENERATOR" \
    -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchains/llvm-mingw.cmake" \
    "-DOPENATL_LLVM_MINGW_BIN=$BIN" \
    "-DOPENATL_TARGET_ARCH=$arch" \
    "-DVCPKG_TARGET_TRIPLET=$triplet" \
    -DOPENATL_BUILD_QUALIFICATION=ON
  cmake --build "$dir" --parallel 2
  python3 tests/wtl10/verify_pe_machine.py "$dir/openatl_gui_runtime.exe" \
    "$arch" --require-resources
  for base in openatl_com_client openatl_thunk_fault openatl_atl_core openatl_atl_path; do
    python3 tests/wtl10/verify_pe_machine.py "$dir/$base.exe" "$arch"
  done
done
echo "OpenATL WTL UI/no-COM + client + isolated W^X fault CMake Windows x64/ARM64 PE PASS (cross-only)"
