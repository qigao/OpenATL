# Only the verified vcpkg WTL10 package is a source provider.
# OpenATL supplies ATL; a compatibility adapter currently operates only on
# a build-local copy of WTL until the full unmodified header API is qualified.
function(_openatl_prepare_wtl)
  if(DEFINED VCPKG_INSTALLED_DIR AND NOT "${VCPKG_INSTALLED_DIR}" STREQUAL "")
    set(_wtl_install "${VCPKG_INSTALLED_DIR}")
  elseif(DEFINED ENV{OPENATL_WTL_VCPKG_INSTALLED_DIR} AND
         NOT "$ENV{OPENATL_WTL_VCPKG_INSTALLED_DIR}" STREQUAL "")
    set(_wtl_install "$ENV{OPENATL_WTL_VCPKG_INSTALLED_DIR}")
  elseif(DEFINED ENV{VCPKG_INSTALLED_DIR} AND NOT "$ENV{VCPKG_INSTALLED_DIR}" STREQUAL "")
    set(_wtl_install "$ENV{VCPKG_INSTALLED_DIR}")
  else()
    message(FATAL_ERROR "Install pinned WTL 10.0.10320#6 with vcpkg-cache overlay before configuring OpenATL")
  endif()
  if(DEFINED VCPKG_TARGET_TRIPLET AND NOT "${VCPKG_TARGET_TRIPLET}" STREQUAL "")
    set(_wtl_triplet "${VCPKG_TARGET_TRIPLET}")
  elseif(DEFINED ENV{OPENATL_WTL_VCPKG_TRIPLET} AND NOT "$ENV{OPENATL_WTL_VCPKG_TRIPLET}" STREQUAL "")
    set(_wtl_triplet "$ENV{OPENATL_WTL_VCPKG_TRIPLET}")
  else()
    message(FATAL_ERROR "Specify VCPKG_TARGET_TRIPLET or OPENATL_WTL_VCPKG_TRIPLET")
  endif()
  if(NOT _wtl_triplet MATCHES "^[a-zA-Z0-9_.+-]+$")
    message(FATAL_ERROR "Invalid WTL triplet")
  endif()
  # Windows GITHUB_ENV paths use backslashes: normalize before CMake serializes sample inputs.
  file(TO_CMAKE_PATH "${_wtl_install}" _wtl_install)
  set(_wtl_pkg "${_wtl_install}/${_wtl_triplet}")
  set(_wtl_source "${_wtl_pkg}/include")
  set(_wtl_share "${_wtl_pkg}/share/wtl")
  set(_wtl_identity "${_wtl_share}/openatl-wtl-source.txt")
  if(NOT EXISTS "${_wtl_source}/atlapp.h" OR
     NOT EXISTS "${_wtl_source}/atldlgs.h" OR
     NOT EXISTS "${_wtl_source}/atlframe.h" OR
     NOT EXISTS "${_wtl_identity}" OR
     NOT EXISTS "${_wtl_share}/copyright" OR
     NOT EXISTS "${_wtl_share}/Samples/GuidGen/GuidGen.cpp" OR
     NOT EXISTS "${_wtl_share}/Samples/WTLExplorer/WTLExplorer.cpp" OR
     NOT EXISTS "${_wtl_share}/AppWizard/Setup.js")
    message(FATAL_ERROR "WTL package incomplete; no SourceForge/vendor fallback is allowed")
  endif()
  if(EXISTS "${_wtl_source}/atlbase.h")
    message(FATAL_ERROR "Vcpkg WTL package unexpectedly supplies a second ATL implementation")
  endif()
  file(READ "${_wtl_identity}" _wtl_actual_identity)
  string(REPLACE "\r\n" "\n" _wtl_actual_identity "${_wtl_actual_identity}")
  set(_wtl_expected_identity "wtl=10.0.10320#6\nsha512=086a6cf6a49a4318a8c519136ba6019ded7aa7f2c1d85f78c30b21183654537b3428a400a64fcdacba3a7a10a9ef05137b6f2119f59594da300d55f9ebfb1309\nprovider=vcpkg-cache/ports/wtl\n")
  if(NOT _wtl_actual_identity STREQUAL _wtl_expected_identity)
    message(FATAL_ERROR "WTL vcpkg source identity mismatch")
  endif()
  set(_openatl_wtl_samples "${_wtl_share}/Samples" PARENT_SCOPE)
  # Private qualification input only: bypass the adapted WTL header tree.
  # Production include paths remain unchanged until the strict gate passes.
  set(_openatl_wtl_original_include "${_wtl_source}" PARENT_SCOPE)
  set(_wtl_adapter "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/wtl/PrepareWtl.cmake")
  set(_wtl_headers "${CMAKE_CURRENT_BINARY_DIR}/wtl/include")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_wtl_identity}" "${_wtl_adapter}")
  file(SHA256 "${_wtl_adapter}" _wtl_adapter_hash)
  file(SHA256 "${_wtl_source}/atlapp.h" _wtl_header_hash)
  set(_wtl_fingerprint "${_wtl_adapter_hash};${_wtl_header_hash};${_wtl_expected_identity}")
  set(_wtl_stamp "${CMAKE_CURRENT_BINARY_DIR}/wtl/adapter.sha256")
  set(_wtl_previous "")
  if(EXISTS "${_wtl_stamp}")
    file(READ "${_wtl_stamp}" _wtl_previous)
  endif()
  if(NOT _wtl_previous STREQUAL _wtl_fingerprint OR NOT EXISTS "${_wtl_headers}/atlapp.h")
    file(REMOVE_RECURSE "${_wtl_headers}")
    file(MAKE_DIRECTORY "${_wtl_headers}")
    file(COPY "${_wtl_source}/" DESTINATION "${_wtl_headers}"
         FILES_MATCHING PATTERN "*.h")
    include("${_wtl_adapter}")
    file(SHA256 "${_wtl_headers}/atlapp.h" _wtl_native_app_hash)
    if(NOT _wtl_native_app_hash STREQUAL _wtl_header_hash)
      message(FATAL_ERROR
        "WTL atlapp.h altered by build-local adapter; original COM/UI server classes are mandatory")
    endif()
    file(WRITE "${_wtl_stamp}" "${_wtl_fingerprint}")
  endif()
  file(SHA256 "${_wtl_headers}/atlapp.h" _wtl_native_app_hash)
  if(NOT _wtl_native_app_hash STREQUAL _wtl_header_hash)
    message(FATAL_ERROR "WTL original CAppModule/CServerAppModule source identity mismatch")
  endif()
  target_include_directories(openatl_atl INTERFACE "${_wtl_headers}")
endfunction()
_openatl_prepare_wtl()
