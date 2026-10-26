#!/usr/bin/env bash
# Cross-compile the native runtime acceptance, do not execute Windows EXEs on the host.
set -euo pipefail
BIN="$(dirname "$(find "$PWD/tools/wtl10-host/llvm" -name x86_64-w64-mingw32-clang++ -print -quit)")"
test -n "$BIN"
WTL_HEADER="$(find "$PWD/tools/wtl10-host/wtl10" -name atlapp.h -print -quit)"
test -f "$WTL_HEADER"
WTL_INC="$(dirname "$WTL_HEADER")"
ATL_INC="$PWD/include"
for cpu in x86_64 aarch64; do
  CXX="$BIN/$cpu-w64-mingw32-clang++"
  RC="$BIN/$cpu-w64-mingw32-windres"
  test -x "$CXX" && test -x "$RC"
  RESOURCE="build-wtl10-host/wtl10_gui_runtime_$cpu.res.o"
  EXE="build-wtl10-host/wtl10_gui_runtime_$cpu.exe"
  "$RC" -i tests/wtl10/gui_runtime.rc -I tests/wtl10 -O coff -o "$RESOURCE"
  "$CXX" -std=c++17 -O0 -DUNICODE -D_UNICODE \
    -D_WIN32_WINNT=0x0601 -DWINVER=0x0601 -D_WIN32_IE=0x0601 \
    "-I$ATL_INC" "-I$WTL_INC" -Itests/wtl10 \
    tests/wtl10/gui_runtime.cpp "$RESOURCE" -o "$EXE" \
    -static -loleaut32 -lole32 -luuid -lcomctl32 -lgdi32 -luser32 \
    -lshell32 -lshlwapi -lcomdlg32 -luxtheme -lmsimg32
  python3 tests/wtl10/verify_pe_machine.py "$EXE" "$cpu" --require-resources

  # W^X fault-only PE is isolated from the normal GUI executable.
  FAULT="build-wtl10-host/wtl10_thunk_fault_$cpu.exe"
  "$CXX" -std=c++17 -O0 -DNDEBUG -DUNICODE -D_UNICODE \
    -DOPENATL_WX_FAULT_TEST=1 -D_WIN32_WINNT=0x0601 \
    "-I$ATL_INC" tests/wtl10/thunk_fault.cpp -o "$FAULT" \
    -static -lole32 -loleaut32 -luuid -luser32 -lgdi32
  python3 tests/wtl10/verify_pe_machine.py "$FAULT" "$cpu"
done
echo "WTL10 GUI + isolated thunk W^X fault PE cross-build (not runtime) x64+ARM64 PASS"
