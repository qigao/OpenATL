add_library(openatl_com_server_fixture SHARED fixture.cpp fixture.def interfaces.h)
set_target_properties(openatl_com_server_fixture PROPERTIES PREFIX "")
target_link_libraries(openatl_com_server_fixture PRIVATE OpenATL::ATL)

add_executable(openatl_com_server_tests server.cpp atl_compat.cpp interfaces.h)
target_link_libraries(openatl_com_server_tests PRIVATE OpenATL::ATL)
add_dependencies(openatl_com_server_tests openatl_com_server_fixture)
set_target_properties(openatl_com_server_fixture openatl_com_server_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

foreach(mode IN ITEMS core sta mta)
  add_test(NAME openatl.com_server_${mode} COMMAND openatl_com_server_tests --${mode})
  set_tests_properties(openatl.com_server_${mode} PROPERTIES
    TIMEOUT 30 RESOURCE_LOCK openatl_com_server_registration)
endforeach()
