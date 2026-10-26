#!/usr/bin/env bash
# Cross-compile Windows WTL10/OpenATL with pinned llvm-mingw + WTL via
# the repo's vcpkg.json and ATL-free WTL overlay port.
set -euo pipefail
test -n "${HOST_LLVM_URL:-}" && test -n "${HOST_LLVM_SHA256:-}"
test -s include/atlbase.h && test -s include/atlwin.h
test ! -e tools/wtl10-host/wtl10 || { echo "Legacy WTL ZIP checkout prohibited" >&2; exit 3; }
mkdir -p tools/wtl10-host/llvm build-wtl10-host
curl -fLsS --retry 3 --retry-delay 3 "$HOST_LLVM_URL" -o build-wtl10-host/llvm.tar.xz
echo "$HOST_LLVM_SHA256  build-wtl10-host/llvm.tar.xz" | shasum -a 256 -c -
tar -xJf build-wtl10-host/llvm.tar.xz -C tools/wtl10-host/llvm
DRIVER="$(find "$PWD/tools/wtl10-host/llvm" -name x86_64-w64-mingw32-clang++ -print -quit)"
test -x "$DRIVER" || { echo "Pinned clang++ driver not found" >&2; exit 2; }
MINGW_BIN="$(dirname "$DRIVER")"
export OPENATL_LLVM_MINGW_BIN="$MINGW_BIN"
export PATH="$MINGW_BIN:$PATH"

VCPKG_SHA=3037cbe976a6872e56e8b07152e19a8344360491
git init -q tools/vcpkg
git -C tools/vcpkg fetch --quiet --depth 1 https://github.com/microsoft/vcpkg.git "$VCPKG_SHA"
git -C tools/vcpkg checkout -q --detach FETCH_HEAD
test "$(git -C tools/vcpkg rev-parse HEAD)" = "$VCPKG_SHA"
bash tools/vcpkg/bootstrap-vcpkg.sh -disableMetrics
VCPKG="$PWD/tools/vcpkg/vcpkg"
for triplet in x64-openatl-mingw arm64-openatl-mingw; do
  "$VCPKG" install "--triplet=$triplet" "--overlay-triplets=$PWD/cmake/vcpkg-triplets"
  WTL_INC="$PWD/vcpkg_installed/$triplet/include"
  test -s "$WTL_INC/atlapp.h" || { echo "Missing vcpkg WTL ($triplet)" >&2; exit 3; }
  test ! -d "$PWD/vcpkg_installed/$triplet/share/atl" || {
    echo "WTL resolved Microsoft ATL stub ($triplet)" >&2; exit 3;
  }
  grep -q 'WTL version 10.0' "$WTL_INC/atlapp.h"
done

# The exact upstream SDI sample is test data only.
git clone --quiet https://github.com/kkhaike/wtltest.git tools/wtl10-host/wtltest
git -C tools/wtl10-host/wtltest checkout --quiet --detach 63ba6a992cb3d79d56be66cd283f6615c6646b3d
test "$(git -C tools/wtl10-host/wtltest rev-parse HEAD)" = 63ba6a992cb3d79d56be66cd283f6615c6646b3d
test -s tools/wtl10-host/wtltest/main.cpp && test -s tools/wtl10-host/wtltest/wtltest.rc
echo "In-tree OpenATL + vcpkg-manifest WTL 10.0.10320 x64/ARM64 ready on $(uname -s)"
