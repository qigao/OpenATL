cmake_minimum_required(VERSION 3.24)
project(OpenATL VERSION 0.1.0 LANGUAGES CXX)

# OpenATL builds Windows-targeting C++ only, including llvm-mingw cross builds
# executed from Linux and macOS hosts.
if(NOT WIN32)
  message(FATAL_ERROR "OpenATL targets Windows; use llvm-mingw with CMAKE_SYSTEM_NAME=Windows")
endif()

set(OPENATL_ATL_INCLUDE_DIR "" CACHE PATH "Pinned external OpenATL include root")
set(OPENATL_WTL_INCLUDE_DIR "" CACHE PATH "Official WTL10 Include directory")
if(NOT EXISTS "${OPENATL_ATL_INCLUDE_DIR}/atlbase.h" OR
   NOT EXISTS "${OPENATL_ATL_INCLUDE_DIR}/atlwin.h")
  message(FATAL_ERROR "OPENATL_ATL_INCLUDE_DIR must point to a valid OpenATL include root")
endif()
if(NOT EXISTS "${OPENATL_WTL_INCLUDE_DIR}/atlapp.h")
  message(FATAL_ERROR "OPENATL_WTL_INCLUDE_DIR must point to official WTL10 Include")
endif()

# Detect accidental use of the Microsoft Visual Studio ATL installation.
# The ONLY ATL provider admitted by this qualification is the pinned
# MIT-licensed clean-room OpenATL root selected by the caller.
file(READ "${OPENATL_ATL_INCLUDE_DIR}/atlbase.h" _openatl_atl_header LIMIT 240)
if(NOT _openatl_atl_header MATCHES "OpenATL - Clean-room ATL subset for WTL 10")
  message(FATAL_ERROR "Refuse non-OpenATL ATL provider header, including MSVC ATL")
endif()

add_library(openatl_atl INTERFACE)
add_library(OpenATL::ATL ALIAS openatl_atl)
target_include_directories(openatl_atl INTERFACE "${OPENATL_ATL_INCLUDE_DIR}")
target_compile_features(openatl_atl INTERFACE cxx_std_17)
target_compile_definitions(openatl_atl INTERFACE UNICODE _UNICODE
  _WIN32_WINNT=0x0601 WINVER=0x0601 _WIN32_IE=0x0601)

add_library(openatl_wtl INTERFACE)
add_library(OpenATL::WTL ALIAS openatl_wtl)
target_include_directories(openatl_wtl INTERFACE "${OPENATL_WTL_INCLUDE_DIR}")
target_link_libraries(openatl_wtl INTERFACE OpenATL::ATL)

option(OPENATL_BUILD_QUALIFICATION "Build isolated WTL/ATL native tests" OFF)
option(OPENATL_TEST_PATCHES_APPLIED "Explicitly assert pinned test-only OpenATL patch" OFF)
if(OPENATL_BUILD_QUALIFICATION)
  if(NOT OPENATL_TEST_PATCHES_APPLIED)
    message(FATAL_ERROR "Qualification requires separately applied pinned test-only OpenATL patches")
  endif()
  enable_language(RC)
  enable_testing()
  add_executable(openatl_gui_runtime
    tests/wtl10/gui_runtime.cpp
    tests/wtl10/gui_runtime.rc)
  target_include_directories(openatl_gui_runtime PRIVATE tests/wtl10)
  target_link_libraries(openatl_gui_runtime PRIVATE OpenATL::WTL
    oleaut32 ole32 uuid comctl32 gdi32 user32 shell32 shlwapi
    comdlg32 uxtheme msimg32)
  add_test(NAME openatl.gui_native COMMAND openatl_gui_runtime)

  add_executable(openatl_com_p0 tests/atl_conformance/com_p0.cpp)
  target_compile_definitions(openatl_com_p0 PRIVATE ATL_CONFORMANCE_OPENATL=1)
  target_link_libraries(openatl_com_p0 PRIVATE OpenATL::ATL
    ole32 oleaut32 uuid rpcrt4 shlwapi advapi32 gdi32 user32)
  add_test(NAME openatl.com_p0 COMMAND openatl_com_p0)

  add_executable(openatl_com_p1 tests/atl_conformance/com_p1.cpp)
  target_link_libraries(openatl_com_p1 PRIVATE OpenATL::ATL
    ole32 oleaut32 uuid rpcrt4 shlwapi advapi32 gdi32 user32)
  add_test(NAME openatl.com_p1 COMMAND openatl_com_p1)

  add_executable(openatl_com_p1b
    tests/atl_conformance/com_p1b_map.cpp
    tests/atl_conformance/com_p1b_entry.cpp)
  target_link_libraries(openatl_com_p1b PRIVATE OpenATL::ATL
    ole32 oleaut32 uuid rpcrt4 shlwapi advapi32 gdi32 user32)
  add_test(NAME openatl.com_p1b COMMAND openatl_com_p1b)

  add_executable(openatl_registry_sandbox tests/atl_conformance/registry_sandbox.cpp)
  target_link_libraries(openatl_registry_sandbox PRIVATE OpenATL::ATL
    ole32 oleaut32 uuid rpcrt4 shlwapi advapi32 gdi32 user32)
  add_test(NAME openatl.registry_sandbox COMMAND openatl_registry_sandbox)

  add_executable(openatl_registrar
    tests/atl_conformance/registry_real.cpp
    tests/atl_conformance/com_p1b_entry.cpp)
  target_link_libraries(openatl_registrar PRIVATE OpenATL::ATL
    ole32 oleaut32 uuid rpcrt4 shlwapi advapi32 gdi32 user32)
  add_test(NAME openatl.registrar_controlled COMMAND openatl_registrar)

  # llvm-mingw's default C++ executables can depend on host-absent
  # libstdc++-6.dll/libwinpthread-1.dll on a clean Windows runner.
  # Qualification must run the *exact* PE without PATH or DLL injection;
  # follow the independently passing direct clang++ -static scripts.
  foreach(_qualified IN ITEMS
      openatl_gui_runtime openatl_com_p0 openatl_com_p1
      openatl_com_p1b openatl_registry_sandbox openatl_registrar)
    target_compile_definitions(${_qualified} PRIVATE NDEBUG)
    if(MSVC)
      # Keep MSVC's own ATL out of the include search. Use OpenATL above.
      # /MT: no dependence on VC++ redistributable DLLs on ARM64 hosts.
      set_property(TARGET ${_qualified} PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
      target_compile_options(${_qualified} PRIVATE /W3 /EHsc)
      target_link_options(${_qualified} PRIVATE /INCREMENTAL:NO /DYNAMICBASE /NXCOMPAT)
    else()
      # Clang++/llvm-mingw native tests previously required static CRT.
      target_link_options(${_qualified} PRIVATE "-static")
    endif()
  endforeach()

  # Compile/link the *original full* WTL10 SDI source and .rc, not only
  # the hidden-window smoke test. Real interactive GUI still not qualified.
  set(OPENATL_WTL_DEMO_DIR "" CACHE PATH "External pinned wtltest root, optional")
  if(NOT OPENATL_WTL_DEMO_DIR STREQUAL "")
    if(NOT EXISTS "${OPENATL_WTL_DEMO_DIR}/main.cpp" OR
       NOT EXISTS "${OPENATL_WTL_DEMO_DIR}/wtltest.rc")
      message(FATAL_ERROR "Full WTL10 sample requires main.cpp and wtltest.rc")
    endif()
    add_executable(openatl_wtl10_sdi WIN32
      "${OPENATL_WTL_DEMO_DIR}/main.cpp"
      "${OPENATL_WTL_DEMO_DIR}/wtltest.rc")
    target_include_directories(openatl_wtl10_sdi PRIVATE "${OPENATL_WTL_DEMO_DIR}")
    target_link_libraries(openatl_wtl10_sdi PRIVATE OpenATL::WTL
      ole32 oleaut32 uuid comctl32 msimg32 uxtheme gdi32 comdlg32
      shell32 shlwapi dwmapi user32)
    if(MSVC)
      set_property(TARGET openatl_wtl10_sdi PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
      target_compile_options(openatl_wtl10_sdi PRIVATE /W3 /EHsc)
      target_link_options(openatl_wtl10_sdi PRIVATE /INCREMENTAL:NO /DYNAMICBASE /NXCOMPAT)
    else()
      target_link_options(openatl_wtl10_sdi PRIVATE -municode -mwindows -static)
    endif()
  endif()
endif()

# Interface targets deliberately are not installed or exported yet.
# External MS-PL WTL10, MIT OpenATL and LGPL/GPL RATL are NOT bundled.
