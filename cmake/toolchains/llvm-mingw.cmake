# llvm-mingw toolchain for Windows-targeting OpenTL C++17 applications.
set(CMAKE_SYSTEM_NAME Windows)

# CMake's ABI try_compile creates a child project. Preserve the target
# and exact compiler root in that child's cache, not only the outer one.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
  OPENTL_TARGET_ARCH OPENTL_LLVM_MINGW_BIN)

if(NOT DEFINED OPENTL_TARGET_ARCH OR
   (NOT OPENTL_TARGET_ARCH STREQUAL "x86_64" AND
    NOT OPENTL_TARGET_ARCH STREQUAL "aarch64"))
  message(FATAL_ERROR "Specify OPENTL_TARGET_ARCH=x86_64 or aarch64")
endif()
if(NOT DEFINED OPENTL_LLVM_MINGW_BIN OR OPENTL_LLVM_MINGW_BIN STREQUAL "")
  message(FATAL_ERROR "Specify OPENTL_LLVM_MINGW_BIN containing SHA-pinned llvm-mingw drivers")
endif()

# Convert Windows PowerShell backslashes before CMake writes compiler files.
file(TO_CMAKE_PATH "${OPENTL_LLVM_MINGW_BIN}" _opentl_bin)
set(_exe "")
if(CMAKE_HOST_WIN32)
  set(_exe ".exe")
endif()
set(CMAKE_SYSTEM_PROCESSOR "${OPENTL_TARGET_ARCH}")
set(CMAKE_C_COMPILER "${_opentl_bin}/${OPENTL_TARGET_ARCH}-w64-mingw32-clang${_exe}")
set(CMAKE_CXX_COMPILER "${_opentl_bin}/${OPENTL_TARGET_ARCH}-w64-mingw32-clang++${_exe}")
set(CMAKE_RC_COMPILER "${_opentl_bin}/${OPENTL_TARGET_ARCH}-w64-mingw32-windres${_exe}")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
