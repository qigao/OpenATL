cmake_minimum_required(VERSION 3.24)
project(OpenATL VERSION 0.1.0 LANGUAGES CXX)

# Only Windows-targeting compilers: llvm-mingw on Windows/Linux/macOS,
# or MSVC cl.exe on Windows. This is UI + optional COM CLIENT only.
if(NOT WIN32)
  message(FATAL_ERROR "OpenATL targets Windows; cross-compile with llvm-mingw")
endif()
# ATL headers are built from this checkout, never an external cache provider.
# There is deliberately no supported OPENATL_ATL_INCLUDE_DIR override.
if(DEFINED OPENATL_ATL_INCLUDE_DIR)
  message(FATAL_ERROR "OPENATL_ATL_INCLUDE_DIR is obsolete; only source-tree include/ is supported")
endif()
set(_openatl_atl_include "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(OPENATL_WTL_INCLUDE_DIR "" CACHE PATH "Official WTL10 Include directory")
if(NOT EXISTS "${_openatl_atl_include}/atlbase.h" OR
   NOT EXISTS "${_openatl_atl_include}/atlwin.h" OR
   NOT EXISTS "${_openatl_atl_include}/atlfile.h" OR
   NOT EXISTS "${_openatl_atl_include}/atlpath.h" OR
   NOT EXISTS "${_openatl_atl_include}/atlsync.h")
  message(FATAL_ERROR "Missing repository-owned OpenATL headers")
endif()
if(NOT EXISTS "${OPENATL_WTL_INCLUDE_DIR}/atlapp.h")
  message(FATAL_ERROR "OPENATL_WTL_INCLUDE_DIR must point to official WTL10 Include")
endif()
file(READ "${_openatl_atl_include}/atlbase.h" _openatl_marker LIMIT 240)
file(READ "${_openatl_atl_include}/atldef.h" _openatl_def_marker LIMIT 2048)
if(NOT _openatl_marker MATCHES "OpenATL - Clean-room ATL subset for WTL 10"
   OR NOT _openatl_def_marker MATCHES "OPENATL_REPOSITORY_HEADERS")
  message(FATAL_ERROR "Refuse missing, external, or unmarked ATL headers")
endif()

# UI requires neither CoInitializeEx nor a COM Server/Object Map.
add_library(openatl_atl INTERFACE)
add_library(OpenATL::ATL ALIAS openatl_atl)
target_include_directories(openatl_atl INTERFACE "${_openatl_atl_include}")
target_compile_features(openatl_atl INTERFACE cxx_std_17)
target_compile_definitions(openatl_atl INTERFACE
  UNICODE _UNICODE _WIN32_WINNT=0x0601 WINVER=0x0601 _WIN32_IE=0x0601)

add_library(openatl_wtl INTERFACE)
add_library(OpenATL::WTL ALIAS openatl_wtl)
target_include_directories(openatl_wtl INTERFACE "${OPENATL_WTL_INCLUDE_DIR}")
target_link_libraries(openatl_wtl INTERFACE OpenATL::ATL)

# Optional client-side COM convenience; not in the UI transitive link closure.
# Consumers explicitly link OpenATL::COMClient and manage their own apartment.
add_library(openatl_comclient INTERFACE)
add_library(OpenATL::COMClient ALIAS openatl_comclient)
target_link_libraries(openatl_comclient INTERFACE
  OpenATL::ATL ole32 oleaut32 uuid)

option(OPENATL_BUILD_QUALIFICATION "Compile native UI, ATL utility and COM client tests" OFF)
if(OPENATL_BUILD_QUALIFICATION)
  enable_language(RC)
  enable_testing()

  add_executable(openatl_gui_runtime
    tests/wtl10/gui_runtime.cpp tests/wtl10/gui_runtime.rc)
  target_include_directories(openatl_gui_runtime PRIVATE tests/wtl10)
  target_link_libraries(openatl_gui_runtime PRIVATE OpenATL::WTL
    # ole32 is used by the test ONLY to prove COM stays uninitialized.
    ole32 comctl32 gdi32 user32 shell32 shlwapi
    comdlg32 uxtheme msimg32 oleaut32 uuid)
  add_test(NAME openatl.ui_no_com COMMAND openatl_gui_runtime)

  # Repository-owned ATL utilities (file/string/sync), independent of COM.
  # ole32 belongs ONLY to this test to verify apartment stays uninitialized.
  add_executable(openatl_atl_core tests/atl_conformance/atl_core.cpp)
  target_link_libraries(openatl_atl_core PRIVATE OpenATL::ATL ole32)
  add_test(NAME openatl.atl_core_no_com COMMAND openatl_atl_core)

  # Repository-owned path and Unicode Win32 file test. No COM dependency.
  add_executable(openatl_atl_path tests/atl_conformance/atl_path.cpp)
  target_link_libraries(openatl_atl_path PRIVATE OpenATL::ATL)
  add_test(NAME openatl.atl_path COMMAND openatl_atl_path)

  # Direct OS failure injection is compiled from the in-repo atlwin.h
  # and isolated behind OPENATL_WX_FAULT_TEST.
  add_executable(openatl_thunk_fault tests/wtl10/thunk_fault.cpp)
  target_compile_definitions(openatl_thunk_fault PRIVATE OPENATL_WX_FAULT_TEST=1)
  target_link_libraries(openatl_thunk_fault PRIVATE OpenATL::ATL)
  add_test(NAME openatl.thunk_fault COMMAND openatl_thunk_fault)

  add_executable(openatl_com_client tests/atl_conformance/com_client.cpp)
  target_link_libraries(openatl_com_client PRIVATE OpenATL::COMClient)
  add_test(NAME openatl.com_client COMMAND openatl_com_client)
  # Every mode is its own process: COM apartment state is thread-local,
  # never shared with WTL UI or another COM client mode.
  foreach(_client_mode IN ITEMS sta mta changed-sta changed-mta uninitialized)
    add_test(NAME "openatl.com_client_${_client_mode}"
             COMMAND openatl_com_client "--${_client_mode}")
  endforeach()

  foreach(_qualified IN ITEMS openatl_gui_runtime openatl_com_client openatl_thunk_fault openatl_atl_core openatl_atl_path)
    target_compile_definitions(${_qualified} PRIVATE NDEBUG)
    if(MSVC)
      set_property(TARGET ${_qualified} PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
      target_compile_options(${_qualified} PRIVATE /W3 /EHsc)
      target_link_options(${_qualified} PRIVATE /INCREMENTAL:NO /DYNAMICBASE /NXCOMPAT)
    else()
      target_link_options(${_qualified} PRIVATE -static)
    endif()
  endforeach()

  set(OPENATL_WTL_DEMO_DIR "" CACHE PATH "External pinned wtltest root, optional")
  if(NOT OPENATL_WTL_DEMO_DIR STREQUAL "")
    if(NOT EXISTS "${OPENATL_WTL_DEMO_DIR}/main.cpp" OR
       NOT EXISTS "${OPENATL_WTL_DEMO_DIR}/wtltest.rc")
      message(FATAL_ERROR "Full WTL10 demo requires main.cpp + wtltest.rc")
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

# No COM server targets, class factories, registry writers, RGS or RATL.
# The clean-room MIT ATL implementation is owned here under include/.
# No checkout patch or external ATL implementation is ever used.
# No install/export until CFG/CET and installed-consumer acceptance.
