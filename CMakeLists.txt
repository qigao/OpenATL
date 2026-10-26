cmake_minimum_required(VERSION 3.24)
project(OpenATL VERSION 0.1.0 LANGUAGES CXX)

# Only Windows-targeting compilers: llvm-mingw on Windows/Linux/macOS,
# or MSVC cl.exe on Windows. This is UI + optional COM CLIENT only.
if(NOT WIN32)
  message(FATAL_ERROR "OpenATL targets Windows; cross-compile with llvm-mingw")
endif()
set(OPENATL_ATL_INCLUDE_DIR "" CACHE PATH "Pinned clean-room OpenATL include root")
set(OPENATL_WTL_INCLUDE_DIR "" CACHE PATH "Official WTL10 Include directory")
if(NOT EXISTS "${OPENATL_ATL_INCLUDE_DIR}/atlbase.h" OR
   NOT EXISTS "${OPENATL_ATL_INCLUDE_DIR}/atlwin.h")
  message(FATAL_ERROR "OPENATL_ATL_INCLUDE_DIR must point to clean-room OpenATL")
endif()
if(NOT EXISTS "${OPENATL_WTL_INCLUDE_DIR}/atlapp.h")
  message(FATAL_ERROR "OPENATL_WTL_INCLUDE_DIR must point to official WTL10 Include")
endif()
file(READ "${OPENATL_ATL_INCLUDE_DIR}/atlbase.h" _openatl_marker LIMIT 240)
if(NOT _openatl_marker MATCHES "OpenATL - Clean-room ATL subset for WTL 10")
  message(FATAL_ERROR "Refuse Microsoft ATL or unknown fallback provider")
endif()

# UI requires neither CoInitializeEx nor a COM Server/Object Map.
add_library(openatl_atl INTERFACE)
add_library(OpenATL::ATL ALIAS openatl_atl)
target_include_directories(openatl_atl INTERFACE "${OPENATL_ATL_INCLUDE_DIR}")
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

option(OPENATL_BUILD_QUALIFICATION "Compile standalone UI and COM client tests" OFF)
option(OPENATL_TEST_PATCHES_APPLIED "Explicitly enable exact-pinned W^X qualification checkout" OFF)
if(OPENATL_BUILD_QUALIFICATION)
  if(NOT OPENATL_TEST_PATCHES_APPLIED)
    message(FATAL_ERROR "Qualification requires pinned temporary OpenATL W^X patch")
  endif()
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

  add_executable(openatl_com_client tests/atl_conformance/com_client.cpp)
  target_link_libraries(openatl_com_client PRIVATE OpenATL::COMClient)
  add_test(NAME openatl.com_client COMMAND openatl_com_client)
  # Every mode is its own process: COM apartment state is thread-local,
  # never shared with WTL UI or another COM client mode.
  foreach(_client_mode IN ITEMS sta mta changed-sta changed-mta uninitialized)
    add_test(NAME "openatl.com_client_${_client_mode}"
             COMMAND openatl_com_client "--${_client_mode}")
  endforeach()

  foreach(_qualified IN ITEMS openatl_gui_runtime openatl_com_client)
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
# No install/export: source-pinned test-only W^X work is not an SDK.
