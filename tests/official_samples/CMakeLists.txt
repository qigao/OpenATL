# Original sources and resources from the checksum-pinned WTL release.
# No source rewriting or manual ATL/WTL include roots.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(samples GuidGen)
# llvm-rc does not implement the release's TOOLBAR resource syntax. Keep
# MemDlg admission explicit to the MSVC resource tool, without rewriting it.
if(MSVC)
  list(APPEND samples MemDlg)
endif()
foreach(sample IN LISTS samples)
  string(TOLOWER "${sample}" name)
  set(target "openatl_official_${name}")
  set(source "${_openatl_wtl_samples}/${sample}")
  add_executable(${target} WIN32 "${source}/${sample}.cpp" "${source}/${sample}.rc")
  target_link_libraries(${target} PRIVATE OpenATL::ATL)
  target_include_directories(${target} PRIVATE "${source}")
  target_compile_definitions(${target} PRIVATE NDEBUG)
  set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
  if(MSVC)
    target_compile_options(${target} PRIVATE /W3 /EHsc)
    target_link_options(${target} PRIVATE /INCREMENTAL:NO /MANIFEST:EMBED
      "/MANIFESTINPUT:${PROJECT_SOURCE_DIR}/tests/wtl10/sdi_comctl6.manifest")
  else()
    target_link_options(${target} PRIVATE -static)
    set(manifest "${CMAKE_CURRENT_BINARY_DIR}/${name}.rc")
    file(WRITE "${manifest}"
      "1 24 \"${PROJECT_SOURCE_DIR}/tests/wtl10/sdi_comctl6.manifest\"\n")
    target_sources(${target} PRIVATE "${manifest}")
  endif()
  add_test(NAME "openatl.official_${name}" COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/wtl_interactive.py" "${name}" "$<TARGET_FILE:${target}>")
  set_tests_properties("openatl.official_${name}" PROPERTIES TIMEOUT 35
    RESOURCE_LOCK openatl_official_wtl_ui)
endforeach()
