# WTL is owned by this dependency boundary, never by a consumer's vcpkg
# manifest. Populate immutable upstream sources; adapt a build-local copy.
function(_openatl_prepare_wtl)
  # The WTL 10.0.10320 source archive is committed to OpenATL.
  # It is the pinned Win32-WTL GitHub mirror release; no SourceForge,
  # package manager, mutable mirror or external WTL download at build time.
  set(_wtl_archive "${PROJECT_SOURCE_DIR}/third_party/wtl/WTL10_10320_Final.zip")
  set(_wtl_sha512 "086a6cf6a49a4318a8c519136ba6019ded7aa7f2c1d85f78c30b21183654537b3428a400a64fcdacba3a7a10a9ef05137b6f2119f59594da300d55f9ebfb1309")
  if(NOT EXISTS "${_wtl_archive}")
    message(FATAL_ERROR "Missing repository-owned WTL 10.0.10320 source archive")
  endif()
  file(SHA512 "${_wtl_archive}" _wtl_actual_sha512)
  if(NOT _wtl_actual_sha512 STREQUAL _wtl_sha512)
    message(FATAL_ERROR "Vendored WTL 10.0.10320 SHA512 mismatch")
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_wtl_archive}")
  include(FetchContent)
  set(FETCHCONTENT_TRY_FIND_PACKAGE_MODE NEVER)
  FetchContent_Declare(openatl_wtl_source
    URL "${_wtl_archive}"
    URL_HASH SHA512=086a6cf6a49a4318a8c519136ba6019ded7aa7f2c1d85f78c30b21183654537b3428a400a64fcdacba3a7a10a9ef05137b6f2119f59594da300d55f9ebfb1309
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(openatl_wtl_source)
  set(_openatl_wtl_samples "${openatl_wtl_source_SOURCE_DIR}/Samples" PARENT_SCOPE)

  set(_wtl_headers "${CMAKE_CURRENT_BINARY_DIR}/wtl/include")
  set(_wtl_adapter "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/wtl/PrepareWtl.cmake")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_wtl_adapter}")
  if(NOT EXISTS "${openatl_wtl_source_SOURCE_DIR}/Include/atlapp.h" OR
     NOT EXISTS "${openatl_wtl_source_SOURCE_DIR}/MS-PL.txt")
    message(FATAL_ERROR "WTL requires the complete vendored 10.0.10320 source tree and MS-PL license")
  endif()
  file(SHA256 "${_wtl_adapter}" _wtl_adapter_hash)
  file(SHA256 "${CMAKE_CURRENT_FUNCTION_LIST_FILE}" _wtl_dependency_hash)
  set(_wtl_adapter_hash "${_wtl_dependency_hash};${_wtl_adapter_hash};${openatl_wtl_source_SOURCE_DIR}")
  set(_wtl_stamp "${CMAKE_CURRENT_BINARY_DIR}/wtl/adapter.sha256")
  set(_wtl_previous "")
  if(EXISTS "${_wtl_stamp}")
    file(READ "${_wtl_stamp}" _wtl_previous)
  endif()
  if(NOT _wtl_previous STREQUAL _wtl_adapter_hash OR NOT EXISTS "${_wtl_headers}/atlapp.h")
    file(MAKE_DIRECTORY "${_wtl_headers}")
    file(COPY "${openatl_wtl_source_SOURCE_DIR}/Include/" DESTINATION "${_wtl_headers}")
    include("${_wtl_adapter}")
    configure_file("${openatl_wtl_source_SOURCE_DIR}/MS-PL.txt"
      "${CMAKE_CURRENT_BINARY_DIR}/wtl/MS-PL.txt" COPYONLY)
    file(WRITE "${_wtl_stamp}" "${_wtl_adapter_hash}")
  endif()
  target_include_directories(openatl_atl INTERFACE "${_wtl_headers}")
endfunction()
_openatl_prepare_wtl()
