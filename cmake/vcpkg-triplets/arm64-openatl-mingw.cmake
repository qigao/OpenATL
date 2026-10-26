# WTL header-only vcpkg overlay cross-compiled with in-tree llvm-mingw.
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME MinGW)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../toolchains/llvm-mingw.cmake")
set(VCPKG_ENV_PASSTHROUGH_UNTRACKED OPENATL_LLVM_MINGW_BIN)
