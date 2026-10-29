# Unlike the current WTL examples, this test must use the ORIGINAL installed
# WTL header tree and never the build-local copy from PrepareWtl.cmake.
if(NOT DEFINED _openatl_wtl_original_include OR
   NOT EXISTS "${_openatl_wtl_original_include}/atlapp.h")
  message(FATAL_ERROR "Original WTL source root is unavailable from verified vcpkg port")
endif()
if(NOT EXISTS "${_openatl_wtl_original_include}/atldlgs.h" OR
   NOT EXISTS "${_openatl_wtl_original_include}/atlmisc.h")
  message(FATAL_ERROR "Original WTL COM/dialog/registry surface is incomplete")
endif()
file(TO_CMAKE_PATH "${_openatl_wtl_original_include}/atlapp.h" OPENATL_WTL_DIRECT_ATLAPP)
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/direct_headers.cpp.in"
  "${CMAKE_CURRENT_BINARY_DIR}/direct_headers.cpp" @ONLY)
add_executable(openatl_wtl_unmodified_headers
  "${CMAKE_CURRENT_BINARY_DIR}/direct_headers.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/legacy_map.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/server_monitor.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/reg_property.cpp")
# Precede the link target's adapted build includes. Direct atlapp.h below is
# an absolute vcpkg source path, and all nested WTL include searches stay here.
target_include_directories(openatl_wtl_unmodified_headers BEFORE PRIVATE
  "${_openatl_wtl_original_include}")
target_link_libraries(openatl_wtl_unmodified_headers PRIVATE OpenATL::ATL)
set_target_properties(openatl_wtl_unmodified_headers PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
target_compile_features(openatl_wtl_unmodified_headers PRIVATE cxx_std_17)
if(MSVC)
  target_compile_options(openatl_wtl_unmodified_headers PRIVATE /W3 /EHsc)
else()
  target_compile_options(openatl_wtl_unmodified_headers PRIVATE -Wall -Wextra)
  if(MINGW)
    # The native Windows llvm-mingw test must never depend on a runner-local
    # libc++/winpthread DLL. Match all existing PE acceptance binaries.
    target_link_options(openatl_wtl_unmodified_headers PRIVATE -static)
  endif()
endif()
add_test(NAME openatl.wtl_unmodified_no_com COMMAND openatl_wtl_unmodified_headers)
