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

git clone --quiet https://github.com/kkhaike/openatl.git tools/wtl10-host/openatl
git -C tools/wtl10-host/openatl checkout --quiet --detach 7b587f3e10f3f8d93a01522033de80007368bad5
test "$(git -C tools/wtl10-host/openatl rev-parse HEAD)" = 7b587f3e10f3f8d93a01522033de80007368bad5
python3 scripts/openatl-wx-hardening.py tools/wtl10-host/openatl
git clone --quiet https://github.com/kkhaike/wtltest.git tools/wtl10-host/wtltest
git -C tools/wtl10-host/wtltest checkout --quiet --detach 63ba6a992cb3d79d56be66cd283f6615c6646b3d
test "$(git -C tools/wtl10-host/wtltest rev-parse HEAD)" = 63ba6a992cb3d79d56be66cd283f6615c6646b3d

ATL_INC="$PWD/tools/wtl10-host/openatl/include"
TEST_DIR="$PWD/tools/wtl10-host/wtltest"
for arch in x86_64 aarch64; do
  CC="$MINGW_BIN/$arch-w64-mingw32-clang++"
  test -x "$CC"
  FLAGS=(-std=c++17 -O0 -DUNICODE -D_UNICODE
    -D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -D_WIN32_IE=0x0601
    "-I$ATL_INC" "-I$WTL_INC" "-I$TEST_DIR")
  "$CC" "${FLAGS[@]}" -fsyntax-only \
    tests/wtl10/openatl_wtl10_bridge.cpp "$TEST_DIR/main.cpp"
  OUT="build-wtl10-host/poc_openatl_wtl10_$arch.dll"
  "$CC" "${FLAGS[@]}" -shared \
    tests/wtl10/openatl_wtl10_bridge.cpp -o "$OUT" \
    -static -loleaut32 -lole32 -luuid -lcomctl32 -lgdi32 -luser32 \
    -lshell32 -lshlwapi -lcomdlg32 -luxtheme -lmsimg32
  python3 tests/wtl10/verify_pe_machine.py "$OUT" "$arch"
done
echo "WTL10/OpenATL cross-build from $(uname -s) for x64+ARM64 PASS"
