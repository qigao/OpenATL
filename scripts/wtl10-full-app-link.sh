#!/usr/bin/env bash
# Link the author's complete WTL10 SDI sample, including PE resources.
# The GUI is NOT executed on a non-Windows build host.
set -euo pipefail
MINGW_CXX="$(find "$PWD/tools/wtl10-host/llvm" -name x86_64-w64-mingw32-clang++ -print -quit)"
test -x "$MINGW_CXX"
BIN="$(dirname "$MINGW_CXX")"
ATL_INC="$PWD/include"
DEMO="$PWD/tools/wtl10-host/wtltest"
test -s "$DEMO/main.cpp" && test -s "$DEMO/wtltest.rc"
for arch in x86_64 aarch64; do
  if [ "$arch" = "x86_64" ]; then
    triplet=x64-openatl-mingw
  else
    triplet=arm64-openatl-mingw
  fi
  WTL_INC="$PWD/vcpkg_installed/$triplet/$triplet/include"
  test -s "$WTL_INC/atlapp.h"
  CC="$BIN/$arch-w64-mingw32-clang++"
  WINDRES="$BIN/$arch-w64-mingw32-windres"
  test -x "$CC" && test -x "$WINDRES"
  RESOURCE="build-wtl10-host/wtltest_$arch.res.o"
  EXE="build-wtl10-host/wtltest_$arch.exe"
  "$WINDRES" -i "$DEMO/wtltest.rc" -I "$DEMO" -O coff -o "$RESOURCE"
  "$CC" -std=c++17 -DUNICODE -D_UNICODE -municode -mwindows \
    -D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -D_WIN32_IE=0x0601 \
    "-I$ATL_INC" "-I$WTL_INC" "-I$DEMO" \
    "$DEMO/main.cpp" "$RESOURCE" -o "$EXE" \
    -static -lole32 -loleaut32 -luuid -lcomctl32 -lmsimg32 \
    -luxtheme -lgdi32 -lcomdlg32 -lshell32 -lshlwapi \
    -ldwmapi -luser32
  python3 tests/wtl10/verify_pe_machine.py "$EXE" "$arch" --require-resources
done
echo "Full WTL10 application SDI+RC link for both Windows PE architectures PASS"
