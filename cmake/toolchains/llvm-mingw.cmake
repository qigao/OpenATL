# llvm-mingw toolchain for Windows-targeting OpenATL C++17 applications.
set(CMAKE_SYSTEM_NAME Windows)

# CMake's ABI try_compile creates a child project. Preserve the target
# and exact compiler root in that child's cache, not only the outer one.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
  OPENATL_TARGET_ARCH OPENATL_LLVM_MINGW_BIN)

# When invoked by vcpkg's compiler detection, derive the arch from its
# triplet. Normal standalone CMake invocations still pass it explicitly.
if(NOT DEFINED OPENATL_TARGET_ARCH AND DEFINED VCPKG_TARGET_ARCHITECTURE)
  if(VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
    set(OPENATL_TARGET_ARCH "x86_64")
  elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "x86")
    set(OPENATL_TARGET_ARCH "i686")
  elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64")
    set(OPENATL_TARGET_ARCH "aarch64")
  endif()
endif()
if(NOT DEFINED OPENATL_LLVM_MINGW_BIN AND DEFINED ENV{OPENATL_LLVM_MINGW_BIN})
  set(OPENATL_LLVM_MINGW_BIN "$ENV{OPENATL_LLVM_MINGW_BIN}" CACHE PATH
    "llvm-mingw compiler bin directory")
endif()

if(NOT DEFINED OPENATL_TARGET_ARCH OR
   (NOT OPENATL_TARGET_ARCH STREQUAL "x86_64" AND
    NOT OPENATL_TARGET_ARCH STREQUAL "aarch64" AND
    NOT OPENATL_TARGET_ARCH STREQUAL "i686"))
  message(FATAL_ERROR "Specify OPENATL_TARGET_ARCH=x86_64, aarch64 or i686")
endif()
if(NOT DEFINED OPENATL_LLVM_MINGW_BIN OR OPENATL_LLVM_MINGW_BIN STREQUAL "")
  message(FATAL_ERROR "Specify OPENATL_LLVM_MINGW_BIN containing SHA-pinned llvm-mingw drivers")
endif()

# Convert Windows PowerShell backslashes before CMake writes compiler files.
file(TO_CMAKE_PATH "${OPENATL_LLVM_MINGW_BIN}" _openatl_bin)
set(_exe "")
if(CMAKE_HOST_WIN32)
  set(_exe ".exe")
endif()
set(CMAKE_SYSTEM_PROCESSOR "${OPENATL_TARGET_ARCH}")
set(CMAKE_C_COMPILER "${_openatl_bin}/${OPENATL_TARGET_ARCH}-w64-mingw32-clang${_exe}")
set(CMAKE_CXX_COMPILER "${_openatl_bin}/${OPENATL_TARGET_ARCH}-w64-mingw32-clang++${_exe}")
if(NOT DEFINED CMAKE_RC_COMPILER)
  set(CMAKE_RC_COMPILER "${_openatl_bin}/${OPENATL_TARGET_ARCH}-w64-mingw32-windres${_exe}")
endif()
# Presets may receive a native SDK path from PowerShell. CMake serializes this
# value into CMakeRCCompiler.cmake, where raw backslashes become invalid escapes.
file(TO_CMAKE_PATH "${CMAKE_RC_COMPILER}" CMAKE_RC_COMPILER)
# Microsoft RC can build the original TOOLBAR resources that llvm-rc rejects.
# Use its own Windows SDK resource headers, without ATL or MFC search roots.
if(CMAKE_HOST_WIN32 AND CMAKE_RC_COMPILER MATCHES "/rc\\.exe$")
  get_filename_component(_openatl_rc_bin "${CMAKE_RC_COMPILER}" DIRECTORY)
  get_filename_component(_openatl_rc_version_dir "${_openatl_rc_bin}" DIRECTORY)
  get_filename_component(_openatl_rc_version "${_openatl_rc_version_dir}" NAME)
  get_filename_component(_openatl_rc_sdk "${_openatl_rc_version_dir}/../.." ABSOLUTE)
  set(CMAKE_RC_STANDARD_INCLUDE_DIRECTORIES
    "${_openatl_rc_sdk}/Include/${_openatl_rc_version}/um"
    "${_openatl_rc_sdk}/Include/${_openatl_rc_version}/shared")
endif()
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
