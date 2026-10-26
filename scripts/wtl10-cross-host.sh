#!/usr/bin/env bash
# Cross-compile Windows WTL10/OpenATL with pinned llvm-mingw on Linux/macOS.
# Native Windows execution is a separate required workflow.
set -euo pipefail
test -n "$HOST_LLVM_URL" && test -n "$HOST_LLVM_SHA256"
mkdir -p tools/wtl10-host/llvm tools/wtl10-host/wtl10 build-wtl10-host
curl -fLsS --retry 3 --retry-delay 3 "$HOST_LLVM_URL" -o build-wtl10-host/llvm.tar.xz
echo "$HOST_LLVM_SHA256  build-wtl10-host/llvm.tar.xz" | shasum -a 256 -c -
tar -xJf build-wtl10-host/llvm.tar.xz -C tools/wtl10-host/llvm
DRIVER="$(find "$PWD/tools/wtl10-host/llvm" -name x86_64-w64-mingw32-clang++ -print -quit)"
test -x "$DRIVER" || { echo "Pinned clang++ driver not found (symlinks allowed)" >&2; find tools/wtl10-host/llvm -maxdepth 3 -name "*clang++*" >&2; exit 2; }
MINGW_BIN="$(dirname "$DRIVER")"

curl -fLsS --retry 3 \
  'https://raw.githubusercontent.com/Win32-WTL/WTL/065ef5597383c1231bd4892cf8e94331b29bdb97/Releases/WTL10_10320_Final.zip' \
  -o build-wtl10-host/wtl10.zip
echo '13de5cd7ed41454b8f287e9bcc4525234c6ac8c85905e4d4fe1a0776709fd78f  build-wtl10-host/wtl10.zip' | shasum -a 256 -c -
test "$(git hash-object build-wtl10-host/wtl10.zip)" = a9f9d84495aac2155b0fdb00fec0e421b0e75c90
unzip -q build-wtl10-host/wtl10.zip -d tools/wtl10-host/wtl10
HEADER="$(find "$PWD/tools/wtl10-host/wtl10" -type f -iname atlapp.h -print -quit)"
test -f "$HEADER" && grep -q 'WTL version 10.0' "$HEADER"
WTL_INC="$(dirname "$HEADER")"

# Repository-owned clean-room MIT headers. Do not fetch a second ATL.
test -s include/atlbase.h && test -s include/atlwin.h
git clone --quiet https://github.com/kkhaike/wtltest.git tools/wtl10-host/wtltest
git -C tools/wtl10-host/wtltest checkout --quiet --detach 63ba6a992cb3d79d56be66cd283f6615c6646b3d
test "$(git -C tools/wtl10-host/wtltest rev-parse HEAD)" = 63ba6a992cb3d79d56be66cd283f6615c6646b3d

ATL_INC="$PWD/include"
TEST_DIR="$PWD/tools/wtl10-host/wtltest"
test -s "$ATL_INC/atlbase.h"
test -s "$ATL_INC/atlwin.h"
test -s "$TEST_DIR/main.cpp"
test -s "$TEST_DIR/wtltest.rc"
# Compiler acceptance now lives in mandatory GUI/COM-client and full SDI
# steps; do NOT compile the removed TinyCC bridge or server/RATL code.
echo "OpenATL in-repo MIT headers + pinned llvm-mingw/WTL10 source ready on $(uname -s)"
