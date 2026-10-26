cmake_minimum_required(VERSION 3.24)
project(OpenTL VERSION 0.1.0 LANGUAGES CXX)

# OpenTL builds Windows-targeting C++ only, including llvm-mingw cross builds
# executed from Linux and macOS hosts.
if(NOT WIN32)
  message(FATAL_ERROR "OpenTL targets Windows; use llvm-mingw with CMAKE_SYSTEM_NAME=Windows")
endif()

set(OPENTL_ATL_INCLUDE_DIR "" CACHE PATH "Pinned external OpenATL include root")
set(OPENTL_WTL_INCLUDE_DIR "" CACHE PATH "Official WTL10 Include directory")
if(NOT EXISTS "${OPENTL_ATL_INCLUDE_DIR}/atlbase.h" OR
   NOT EXISTS "${OPENTL_ATL_INCLUDE_DIR}/atlwin.h")
  message(FATAL_ERROR "OPENTL_ATL_INCLUDE_DIR must point to a valid OpenATL include root")
endif()
if(NOT EXISTS "${OPENTL_WTL_INCLUDE_DIR}/atlapp.h")
  message(FATAL_ERROR "OPENTL_WTL_INCLUDE_DIR must point to official WTL10 Include")
endif()

add_library(opentl_atl INTERFACE)
add_library(OpenTL::ATL ALIAS opentl_atl)
target_include_directories(opentl_atl INTERFACE "${OPENTL_ATL_INCLUDE_DIR}")
target_compile_features(opentl_atl INTERFACE cxx_std_17)
target_compile_definitions(opentl_atl INTERFACE UNICODE _UNICODE
  _WIN32_WINNT=0x0601 WINVER=0x0601 _WIN32_IE=0x0601)

add_library(opentl_wtl INTERFACE)
add_library(OpenTL::WTL ALIAS opentl_wtl)
target_include_directories(opentl_wtl INTERFACE "${OPENTL_WTL_INCLUDE_DIR}")
target_link_libraries(opentl_wtl INTERFACE OpenTL::ATL)

option(OPENTL_BUILD_QUALIFICATION "Build isolated WTL/ATL native tests" OFF)
option(OPENTL_TEST_PATCHES_APPLIED "Explicitly assert pinned test-only OpenATL patch" OFF)
if(OPENTL_BUILD_QUALIFICATION)
  if(NOT OPENTL_TEST_PATCHES_APPLIED)
    message(FATAL_ERROR "Qualification requires separately applied pinned test-only OpenATL patches")
  endif()
  enable_language(RC)
  enable_testing()
  add_executable(opentl_gui_runtime
    tests/wtl10/gui_runtime.cpp
    tests/wtl10/gui_runtime.rc)
  target_include_directories(opentl_gui_runtime PRIVATE tests/wtl10)
  target_link_libraries(opentl_gui_runtime PRIVATE OpenTL::WTL
    oleaut32 ole32 uuid comctl32 gdi32 user32 shell32 shlwapi
    comdlg32 uxtheme msimg32)
  add_test(NAME opentl.gui_native COMMAND opentl_gui_runtime)

  add_executable(opentl_com_p0 tests/atl_conformance/com_p0.cpp)
  target_compile_definitions(opentl_com_p0 PRIVATE ATL_CONFORMANCE_OPENATL=1)
  target_link_libraries(opentl_com_p0 PRIVATE OpenTL::ATL
    ole32 oleaut32 uuid rpcrt4 shlwapi advapi32 gdi32 user32)
  add_test(NAME opentl.com_p0 COMMAND opentl_com_p0)

  add_executable(opentl_com_p1 tests/atl_conformance/com_p1.cpp)
  target_link_libraries(opentl_com_p1 PRIVATE OpenTL::ATL
    ole32 oleaut32 uuid rpcrt4 shlwapi advapi32 gdi32 user32)
  add_test(NAME opentl.com_p1 COMMAND opentl_com_p1)

  add_executable(opentl_com_p1b
    tests/atl_conformance/com_p1b_map.cpp
    tests/atl_conformance/com_p1b_entry.cpp)
  target_link_libraries(opentl_com_p1b PRIVATE OpenTL::ATL
    ole32 oleaut32 uuid rpcrt4 shlwapi advapi32 gdi32 user32)
  add_test(NAME opentl.com_p1b COMMAND opentl_com_p1b)

  add_executable(opentl_registry_sandbox tests/atl_conformance/registry_sandbox.cpp)
  target_link_libraries(opentl_registry_sandbox PRIVATE OpenTL::ATL
    ole32 oleaut32 uuid rpcrt4 shlwapi advapi32 gdi32 user32)
  add_test(NAME opentl.registry_sandbox COMMAND opentl_registry_sandbox)

  add_executable(opentl_registrar
    tests/atl_conformance/registry_real.cpp
    tests/atl_conformance/com_p1b_entry.cpp)
  target_link_libraries(opentl_registrar PRIVATE OpenTL::ATL
    ole32 oleaut32 uuid rpcrt4 shlwapi advapi32 gdi32 user32)
  add_test(NAME opentl.registrar_controlled COMMAND opentl_registrar)
endif()

# Interface targets deliberately are not installed or exported yet.
# External MS-PL WTL10, MIT OpenATL and LGPL/GPL RATL are NOT bundled.
