# Compile the original sample sources, never RATL's implementation or its
# upstream CMake configuration (which can select Microsoft ATL on MSVC).
include(FetchContent)
FetchContent_Declare(openatl_ratl_samples
  URL "https://codeload.github.com/katahiromz/RATL/zip/2e2efa182d7c6ee9037454d23780991fb8543084"
  URL_HASH SHA256=070b29862ae2c52bd023ea54b7cdbc61f3dbe8df70346d29ccbc0c51980e913b
  SOURCE_SUBDIR samples
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE TLS_VERIFY TRUE)
FetchContent_MakeAvailable(openatl_ratl_samples)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
foreach(sample IN ITEMS Console Dialog Window)
  string(TOLOWER "${sample}" name)
  set(target "openatl_ratl_${name}")
  set(source "${openatl_ratl_samples_SOURCE_DIR}/samples/Atl${sample}App")
  add_executable(${target} "${source}/main.cpp" "${source}/main_res.rc")
  target_include_directories(${target} PRIVATE "${source}")
  target_link_libraries(${target} PRIVATE OpenATL::ATL)
  target_compile_definitions(${target} PRIVATE NDEBUG)
  set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
  if(NOT sample STREQUAL "Console")
    set_target_properties(${target} PROPERTIES WIN32_EXECUTABLE TRUE OPENATL_USE_WINMAIN TRUE)
  endif()
  if(MSVC)
    target_compile_options(${target} PRIVATE /W3 /EHsc)
    target_link_options(${target} PRIVATE /INCREMENTAL:NO /MANIFEST:NO)
  else()
    target_link_options(${target} PRIVATE -static)
  endif()
  add_test(NAME "openatl.ratl_${name}" COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/behavior.py" "${name}" "$<TARGET_FILE:${target}>")
  set_tests_properties("openatl.ratl_${name}" PROPERTIES TIMEOUT 35
    RESOURCE_LOCK openatl_ratl_registry)
endforeach()
