# Build actual type REGISTRY Win32 resource blobs. The two ordinary
# fixture scripts match the test COM CLSIDs; the other two files are the
# unchanged, checksum-pinned Microsoft ATL RGS resources from #30.
file(TO_CMAKE_PATH "${PROJECT_SOURCE_DIR}/tests/com_server/rgs_value.rgs"
  _openatl_fixture_value_rgs)
file(TO_CMAKE_PATH "${PROJECT_SOURCE_DIR}/tests/com_server/rgs_aux.rgs"
  _openatl_fixture_aux_rgs)
file(TO_CMAKE_PATH "${PROJECT_SOURCE_DIR}/tests/atl_rgs/fixtures/Chain.rgs"
  _openatl_fixture_chain_rgs)
file(TO_CMAKE_PATH "${PROJECT_SOURCE_DIR}/tests/atl_rgs/fixtures/Outer.rgs"
  _openatl_fixture_outer_rgs)
set(_openatl_fixture_rc "${CMAKE_CURRENT_BINARY_DIR}/fixture_registry.rc")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/fixture_registry.rc.in"
  "${_openatl_fixture_rc}" @ONLY)
add_library(openatl_com_server_fixture SHARED
  fixture.cpp fixture.def fixture_registry_ids.h
  "${_openatl_fixture_rc}" interfaces.h)
set_target_properties(openatl_com_server_fixture PROPERTIES PREFIX "")
target_link_libraries(openatl_com_server_fixture PRIVATE OpenATL::ATL)

# Four unchanged, pinned Microsoft ATL RGS samples: verify their original
# Git blobs at CMake configure time and embed their bytes in Windows-native
# tests. No registry writes or RGS execution is done by the parser.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(_rgs_src "${PROJECT_SOURCE_DIR}/tests/atl_rgs")
set(_rgs_fixtures
  "${_rgs_src}/fixtures/Chain.rgs"
  "${_rgs_src}/fixtures/Outer.rgs"
  "${_rgs_src}/fixtures/ArrayManager.rgs"
  "${_rgs_src}/fixtures/MapCollection.rgs")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_rgs_fixtures})
execute_process(COMMAND "${Python3_EXECUTABLE}"
  "${_rgs_src}/embed_fixtures.py"
  "${CMAKE_CURRENT_BINARY_DIR}/openatl_rgs_fixtures.generated.h"
  "${_rgs_src}/fixtures"
  COMMAND_ERROR_IS_FATAL ANY)

add_executable(openatl_com_server_tests
  server.cpp atl_compat.cpp atl_module.cpp atl_dispatch.cpp
  rgs_parser_tests.cpp rgs_registry_tests.cpp interfaces.h)
target_include_directories(openatl_com_server_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
# Inline RGS registration functions must have identical definitions in
# EVERY translation unit of the test executable. Otherwise the test-only
# fault hook can be optimized out by a differently compiled inline copy.
# This define applies only to the native TEST executable, never the DLL.
target_compile_definitions(openatl_com_server_tests PRIVATE OPENATL_RGS_TEST_FAULT=1)
target_link_libraries(openatl_com_server_tests PRIVATE OpenATL::ATL)
add_dependencies(openatl_com_server_tests openatl_com_server_fixture)
set_target_properties(openatl_com_server_fixture openatl_com_server_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

foreach(mode IN ITEMS core sta mta module)
  add_test(NAME openatl.com_server_${mode} COMMAND openatl_com_server_tests --${mode})
  set_tests_properties(openatl.com_server_${mode} PROPERTIES
    TIMEOUT 30 RESOURCE_LOCK openatl_com_server_registration)
endforeach()
