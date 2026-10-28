cmake_minimum_required(VERSION 3.24)
project(OpenATL VERSION 0.1.0 LANGUAGES CXX)

# Only Windows-targeting compilers: llvm-mingw on Windows/Linux/macOS,
# or MSVC cl.exe on Windows. All APIs use the OpenATL::ATL target.
if(NOT WIN32)
  message(FATAL_ERROR "OpenATL targets Windows; cross-compile with llvm-mingw")
endif()
# ATL headers are built from this checkout, never an external cache provider.
# There is deliberately no supported OPENATL_ATL_INCLUDE_DIR override.
if(DEFINED OPENATL_ATL_INCLUDE_DIR)
  message(FATAL_ERROR "OPENATL_ATL_INCLUDE_DIR is obsolete; only source-tree include/ is supported")
endif()
set(_openatl_atl_include "${CMAKE_CURRENT_SOURCE_DIR}/include")
if(NOT EXISTS "${_openatl_atl_include}/atlbase.h" OR
   NOT EXISTS "${_openatl_atl_include}/atlwin.h" OR
   NOT EXISTS "${_openatl_atl_include}/atlfile.h" OR
   NOT EXISTS "${_openatl_atl_include}/atlpath.h" OR
   NOT EXISTS "${_openatl_atl_include}/atlsync.h" OR
   NOT EXISTS "${_openatl_atl_include}/atlmem.h" OR
   NOT EXISTS "${_openatl_atl_include}/atltime.h" OR
   NOT EXISTS "${_openatl_atl_include}/atlcoll.h" OR
   NOT EXISTS "${_openatl_atl_include}/atlimage.h")
  message(FATAL_ERROR "Missing repository-owned OpenATL headers")
endif()
if(DEFINED OPENATL_WTL_INCLUDE_DIR)
  message(FATAL_ERROR "OPENATL_WTL_INCLUDE_DIR is obsolete; OpenATL manages its pinned WTL dependency")
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

# One consumer target supplies the system dependencies for COM helpers and
# explicit registration. Linking these imports does not initialize COM or
# register any class; callers still opt into those operations explicitly.
target_link_libraries(openatl_atl INTERFACE
  ole32 oleaut32 uuid advapi32 comctl32 gdi32 user32 shell32 shlwapi
  comdlg32 uxtheme msimg32 dwmapi)
if(MINGW)
  # WTL's _tWinMain is wWinMain in this Unicode-only library. Apply the
  # matching CRT entry only to GUI executables, leaving console tests alone.
  target_link_options(openatl_atl INTERFACE
    "$<$<AND:$<BOOL:$<TARGET_PROPERTY:WIN32_EXECUTABLE>>,$<NOT:$<BOOL:$<TARGET_PROPERTY:OPENATL_USE_WINMAIN>>>>:-municode>")
endif()
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/OpenATLWTL.cmake")

# Use the compiler target, never a vcpkg triplet or the build host's CPU.
if(MSVC)
  set(_openatl_target_arch "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}")
else()
  set(_openatl_target_arch "${CMAKE_SYSTEM_PROCESSOR}")
endif()
set(_openatl_x64 FALSE)
if(_openatl_target_arch MATCHES "^(x64|x86_64|AMD64|amd64)$")
  set(_openatl_x64 TRUE)
endif()

option(OPENATL_USE_SALTS_UNICODE "Enable explicit Unicode 17 text processing with an installed Salts SDK" OFF)
if(OPENATL_USE_SALTS_UNICODE)
  if(NOT DEFINED ENV{SALTS_ROOT} OR "$ENV{SALTS_ROOT}" STREQUAL "" OR
     NOT IS_DIRECTORY "$ENV{SALTS_ROOT}")
    message(FATAL_ERROR "OPENATL_USE_SALTS_UNICODE requires SALTS_ROOT pointing to a matching installed SDK")
  endif()
  find_package(Salts 2.3 CONFIG REQUIRED PATHS "$ENV{SALTS_ROOT}" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
  file(REAL_PATH "$ENV{SALTS_ROOT}" _openatl_salts_root)
  file(REAL_PATH "${Salts_DIR}" _openatl_salts_config)
  cmake_path(IS_PREFIX _openatl_salts_root "${_openatl_salts_config}" NORMALIZE _openatl_salts_owned)
  if(NOT _openatl_salts_owned OR NOT TARGET Salts::Unicode)
    message(FATAL_ERROR "The configured SALTS_ROOT must supply Salts::Unicode; cached or alternate SDKs are unsupported")
  endif()
  target_link_libraries(openatl_atl INTERFACE Salts::Unicode)
  # Salts platform headers use Winsock2. Exclude the legacy Winsock header
  # before any consumer includes windows.h, regardless of header order.
  target_compile_definitions(openatl_atl INTERFACE OPENATL_USE_SALTS_UNICODE=1 WIN32_LEAN_AND_MEAN)
endif()

# Qualification defaults to static CRT. An SDK-consuming profile can select
# its matching CRT at the standard CMake configuration owner.
if(MSVC AND NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(OPENATL_BUILD_QUALIFICATION "Compile native UI, ATL utility and COM client/server tests" OFF)
if(OPENATL_BUILD_QUALIFICATION)
  enable_language(RC)
  enable_testing()

  add_executable(openatl_gui_runtime
    tests/wtl10/gui_runtime.cpp tests/wtl10/gui_runtime.rc)
  target_include_directories(openatl_gui_runtime PRIVATE tests/wtl10)
  target_link_libraries(openatl_gui_runtime PRIVATE OpenATL::ATL)
  add_test(NAME openatl.ui_no_com COMMAND openatl_gui_runtime)

  # Repository-owned ATL utilities (file/string/sync), independent of COM.
  # ole32 belongs ONLY to this test to verify apartment stays uninitialized.
  add_executable(openatl_atl_core tests/atl_conformance/atl_core.cpp)
  target_link_libraries(openatl_atl_core PRIVATE OpenATL::ATL ole32 advapi32)
  add_test(NAME openatl.atl_core_no_com COMMAND openatl_atl_core)

  # Repository-owned path and Unicode Win32 file test. No COM dependency.
  add_executable(openatl_atl_path tests/atl_conformance/atl_path.cpp)
  target_link_libraries(openatl_atl_path PRIVATE OpenATL::ATL)
  add_test(NAME openatl.atl_path COMMAND openatl_atl_path)

  add_executable(openatl_unicode tests/atl_conformance/unicode.cpp)
  target_link_libraries(openatl_unicode PRIVATE OpenATL::ATL)
  add_test(NAME openatl.unicode COMMAND openatl_unicode)
  add_executable(openatl_unicode_utf8 tests/atl_conformance/unicode.cpp)
  target_link_libraries(openatl_unicode_utf8 PRIVATE OpenATL::ATL)
  if(MSVC)
    target_link_options(openatl_unicode_utf8 PRIVATE /MANIFEST:EMBED
      "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/tests/atl_conformance/unicode_utf8.manifest")
  else()
    set(_unicode_manifest_rc "${CMAKE_CURRENT_BINARY_DIR}/openatl_unicode_utf8.rc")
    file(WRITE "${_unicode_manifest_rc}"
      "1 24 \"${CMAKE_CURRENT_SOURCE_DIR}/tests/atl_conformance/unicode_utf8.manifest\"\n")
    target_sources(openatl_unicode_utf8 PRIVATE "${_unicode_manifest_rc}")
  endif()
  add_test(NAME openatl.unicode_utf8_acp COMMAND openatl_unicode_utf8 --utf8-acp)

  # Additional source-owned ATL-style heap, time and typed array utilities.
  # ole32 is used only by the native test to detect accidental COM init.
  add_executable(openatl_atl_extensions tests/atl_conformance/atl_extensions.cpp)
  target_link_libraries(openatl_atl_extensions PRIVATE OpenATL::ATL ole32)
  add_test(NAME openatl.atl_extensions_no_com COMMAND openatl_atl_extensions)

  # Actual Win32 file/shared-memory mappings, offsets and handle lifetime.
  # No COM initialization: ole32 is only linked by the no-apartment test.
  add_executable(openatl_atl_mapping tests/atl_conformance/atl_mapping.cpp)
  target_link_libraries(openatl_atl_mapping PRIVATE OpenATL::ATL ole32)
  add_test(NAME openatl.atl_mapping_no_com COMMAND openatl_atl_mapping)

  # GDI bitmap owners plus optional BMP/PNG GDI+ codec calls. No COM
  # initialization; ole32 is linked only for the no-apartment assertion.
  # Consumers calling CImage::Load/Save must link Windows gdiplus.
  add_executable(openatl_atl_image tests/atl_conformance/atl_image.cpp)
  target_link_libraries(openatl_atl_image PRIVATE OpenATL::ATL gdi32 gdiplus ole32)
  add_test(NAME openatl.atl_image_no_com COMMAND openatl_atl_image)

  # Direct OS failure injection is compiled from the in-repo atlwin.h
  # and isolated behind OPENATL_WX_FAULT_TEST.
  add_executable(openatl_mitigation_runtime tests/wtl10/mitigation_runtime.cpp)
  target_link_libraries(openatl_mitigation_runtime PRIVATE OpenATL::ATL)
  add_test(NAME openatl.mitigation_runtime COMMAND openatl_mitigation_runtime)
  # Opt into real toolchain CFG/CET on this isolated native binary. Its
  # PE and process policies are separately checked; these flags alone
  # never count as mitigation evidence for downstream consumers.
  if(MSVC)
    target_compile_options(openatl_mitigation_runtime PRIVATE /guard:cf)
    target_link_options(openatl_mitigation_runtime PRIVATE /GUARD:CF /DYNAMICBASE /NXCOMPAT)
    if(_openatl_x64)
      target_link_options(openatl_mitigation_runtime PRIVATE /CETCOMPAT)
    endif()
  else()
    target_compile_options(openatl_mitigation_runtime PRIVATE -Xclang -cfguard)
    target_link_options(openatl_mitigation_runtime PRIVATE -Wl,-Xlink,-guard:cf)
    if(_openatl_x64)
      target_link_options(openatl_mitigation_runtime PRIVATE -Wl,-Xlink,-cetcompat)
    endif()
  endif()

  # x64 CETCOMPAT metadata and native user shadow-stack enforcement are
  # distinct. By default inventory is nonblocking and reports UNQUALIFIED
  # where the hosted Windows machine does not enforce HSP.
  option(OPENATL_REQUIRE_CET_RUNTIME "Require actual x64 user shadow-stack violation" OFF)
  if(_openatl_x64)
    add_executable(openatl_cet_shadow_stack tests/wtl10/cet_shadow_stack.cpp)
    target_link_libraries(openatl_cet_shadow_stack PRIVATE OpenATL::ATL)
    if(MSVC)
      target_compile_options(openatl_cet_shadow_stack PRIVATE /guard:cf /W3 /EHsc)
      target_link_options(openatl_cet_shadow_stack PRIVATE
        /GUARD:CF /CETCOMPAT /DYNAMICBASE /NXCOMPAT /INCREMENTAL:NO)
    else()
      target_compile_options(openatl_cet_shadow_stack PRIVATE
        -Xclang -cfguard -fno-omit-frame-pointer -fno-optimize-sibling-calls)
      target_link_options(openatl_cet_shadow_stack PRIVATE
        -Wl,-Xlink,-guard:cf -Wl,-Xlink,-cetcompat -static)
    endif()
    add_test(NAME openatl.cet_inventory COMMAND openatl_cet_shadow_stack --inventory)
    if(OPENATL_REQUIRE_CET_RUNTIME)
      add_test(NAME openatl.cet_hardware_required
        COMMAND openatl_cet_shadow_stack --require-hardware)
      set_tests_properties(openatl.cet_hardware_required PROPERTIES TIMEOUT 20)
    endif()
  elseif(OPENATL_REQUIRE_CET_RUNTIME)
    message(FATAL_ERROR "Real x64 CET user shadow-stack tests cannot run on ARM64")
  endif()

  # Real test DLL, not a substitute for production plugin lifetime rules.
  # The host uses LoadLibrary/GetProcAddress, never a static import.
  add_library(openatl_unload_fixture SHARED tests/wtl10/unload_fixture.cpp)
  target_link_libraries(openatl_unload_fixture PRIVATE OpenATL::ATL user32)
  set_target_properties(openatl_unload_fixture PROPERTIES
    PREFIX "" OUTPUT_NAME "openatl_unload_fixture")
  add_executable(openatl_unload_runner tests/wtl10/unload_runner.cpp)
  target_link_libraries(openatl_unload_runner PRIVATE OpenATL::ATL ole32 user32)
  add_dependencies(openatl_unload_runner openatl_unload_fixture)
  add_test(NAME openatl.dll_unload_native COMMAND openatl_unload_runner)
  set_tests_properties(openatl.dll_unload_native PROPERTIES TIMEOUT 35)

  # Independent blocked-callback probe from PR #9, alongside the queued-work
  # and caller-lease unload fixture above.
  add_library(openatl_unload_probe SHARED tests/wtl10/unload_probe.cpp)
  set_target_properties(openatl_unload_probe PROPERTIES PREFIX "")
  target_link_libraries(openatl_unload_probe PRIVATE OpenATL::ATL user32)
  add_executable(openatl_unload_native tests/wtl10/unload_native.cpp)
  target_link_libraries(openatl_unload_native PRIVATE OpenATL::ATL ole32 user32)
  add_dependencies(openatl_unload_native openatl_unload_probe)
  add_test(NAME openatl.dll_callback_unload COMMAND openatl_unload_native)
  set_tests_properties(openatl.dll_callback_unload PROPERTIES TIMEOUT 30)

  add_executable(openatl_thunk_fault tests/wtl10/thunk_fault.cpp)
  target_compile_definitions(openatl_thunk_fault PRIVATE OPENATL_WX_FAULT_TEST=1)
  target_link_libraries(openatl_thunk_fault PRIVATE OpenATL::ATL)
  add_test(NAME openatl.thunk_fault COMMAND openatl_thunk_fault)

  add_executable(openatl_com_client tests/atl_conformance/com_client.cpp)
  target_link_libraries(openatl_com_client PRIVATE OpenATL::ATL)
  add_test(NAME openatl.com_client COMMAND openatl_com_client)
  # Every mode is its own process: COM apartment state is thread-local,
  # never shared with WTL UI or another COM client mode.
  foreach(_client_mode IN ITEMS sta mta changed-sta changed-mta uninitialized)
    add_test(NAME "openatl.com_client_${_client_mode}"
             COMMAND openatl_com_client "--${_client_mode}")
  endforeach()

  add_subdirectory(tests/com_server)
  add_subdirectory(tests/official_samples)
  add_subdirectory(tests/ratl_samples)

  foreach(_qualified IN ITEMS openatl_gui_runtime openatl_com_client openatl_unicode openatl_unicode_utf8 openatl_com_server_fixture openatl_com_server_tests openatl_thunk_fault openatl_mitigation_runtime openatl_unload_fixture openatl_unload_runner openatl_unload_probe openatl_unload_native openatl_atl_core openatl_atl_path openatl_atl_extensions openatl_atl_mapping openatl_atl_image)
    target_compile_definitions(${_qualified} PRIVATE NDEBUG)
    if(MSVC)
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
    # The pinned SDI uses SysLink (comctl32 v6). MSVC already injects a
    # default RT_MANIFEST: merge its dependencies, do not add a duplicate.
    # MinGW/LLD does not generate that resource, so embed one there.
    if(MSVC)
      target_link_options(openatl_wtl10_sdi PRIVATE
        "/MANIFEST:EMBED"
        "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/tests/wtl10/sdi_comctl6.manifest")
    else()
      set(_sdi_manifest_rc "${CMAKE_CURRENT_BINARY_DIR}/openatl_sdi_comctl6.rc")
      file(WRITE "${_sdi_manifest_rc}"
        "1 24 \"${CMAKE_CURRENT_SOURCE_DIR}/tests/wtl10/sdi_comctl6.manifest\"\n")
      target_sources(openatl_wtl10_sdi PRIVATE "${_sdi_manifest_rc}")
    endif()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME openatl.wtl10_sdi_native_ui
      COMMAND "${Python3_EXECUTABLE}"
              "${CMAKE_CURRENT_SOURCE_DIR}/tests/wtl10/sdi_interactive.py"
              "$<TARGET_FILE:openatl_wtl10_sdi>")
    set_tests_properties(openatl.wtl10_sdi_native_ui PROPERTIES TIMEOUT 35)
    target_include_directories(openatl_wtl10_sdi PRIVATE "${OPENATL_WTL_DEMO_DIR}")
    target_link_libraries(openatl_wtl10_sdi PRIVATE OpenATL::ATL)
    if(MSVC)
      target_compile_options(openatl_wtl10_sdi PRIVATE /W3 /EHsc)
      target_link_options(openatl_wtl10_sdi PRIVATE /INCREMENTAL:NO /DYNAMICBASE /NXCOMPAT)
    else()
      target_link_options(openatl_wtl10_sdi PRIVATE -static)
    endif()
  endif()
endif()

# Nonaggregated ATL object compatibility is explicit in atlcom.h; no obsolete module, RGS or RATL provider.
# The clean-room MIT ATL implementation is owned here under include/.
# No checkout patch or external ATL implementation is ever used.
# Permanent source-tree-only API: no install(), export(), packaging, or
# installed-consumer acceptance. This project never distributes an Installed SDK.
