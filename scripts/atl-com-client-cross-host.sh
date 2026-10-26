#!/usr/bin/env bash
# Client-only: no RATL, COM servers, class factories or registry.
set -euo pipefail
CXX="$(find "$PWD/tools/wtl10-host/llvm" -name x86_64-w64-mingw32-clang++ -print -quit)"
test -x "$CXX"
BIN="$(dirname "$CXX")"
ATL="$PWD"
test -s "$ATL/include/atlbase.h" && test -s "$ATL/include/atlcomcli.h"
for arch in x86_64 aarch64; do
  compiler="$BIN/$arch-w64-mingw32-clang++"
  test -x "$compiler"
  result="build-wtl10-host/atl_com_client_$arch.exe"
  "$compiler" -std=c++17 -O0 -DNDEBUG -DUNICODE -D_UNICODE \
    -D_WIN32_WINNT=0x0601 "-I$ATL/include" \
    tests/atl_conformance/com_client.cpp -o "$result" \
    -static -lole32 -loleaut32 -luuid -lrpcrt4 \
    -lshlwapi -ladvapi32 -lgdi32 -luser32 -lkernel32
  python3 tests/wtl10/verify_pe_machine.py "$result" "$arch"
done
echo 'OpenATL COM client-only Windows x64+ARM64 PE cross-build PASS'
