# vcpkg manages WTL headers; llvm-mingw builds the actual Windows binaries.
# Do not use vcpkg's default MSVC ATL dependency or system ATL.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME MinGW)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../toolchains/llvm-mingw.cmake")
set(VCPKG_ENV_PASSTHROUGH_UNTRACKED OPENATL_LLVM_MINGW_BIN)
