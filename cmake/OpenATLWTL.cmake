# Canonical source: the single SHA512-pinned vcpkg-cache WTL10 package.
# OpenATL supplies ATL/COM; all targets include the installed WTL directly.
# No source copying, local rewrite, alternate provider or fallback exists.
option(OPENATL_WTL_FULL_ORIGINAL
  "Use only SHA512-verified original vcpkg WTL10 headers; no local adapter" ON)
function(_openatl_prepare_wtl)
  if(DEFINED VCPKG_INSTALLED_DIR AND NOT "${VCPKG_INSTALLED_DIR}" STREQUAL "")
    set(_wtl_install "${VCPKG_INSTALLED_DIR}")
  elseif(DEFINED ENV{OPENATL_WTL_VCPKG_INSTALLED_DIR} AND
         NOT "$ENV{OPENATL_WTL_VCPKG_INSTALLED_DIR}" STREQUAL "")
    set(_wtl_install "$ENV{OPENATL_WTL_VCPKG_INSTALLED_DIR}")
  elseif(DEFINED ENV{VCPKG_INSTALLED_DIR} AND NOT "$ENV{VCPKG_INSTALLED_DIR}" STREQUAL "")
    set(_wtl_install "$ENV{VCPKG_INSTALLED_DIR}")
  else()
    message(FATAL_ERROR "Install pinned WTL 10.0.10320#7 with vcpkg-cache overlay before configuring OpenATL")
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
  set(_wtl_expected_identity "wtl=10.0.10320#7\nsha512=086a6cf6a49a4318a8c519136ba6019ded7aa7f2c1d85f78c30b21183654537b3428a400a64fcdacba3a7a10a9ef05137b6f2119f59594da300d55f9ebfb1309\nprovider=vcpkg-cache/ports/wtl\n")
  if(NOT _wtl_actual_identity STREQUAL _wtl_expected_identity)
    message(FATAL_ERROR "WTL vcpkg source identity mismatch")
  endif()
  set(_openatl_wtl_samples "${_wtl_share}/Samples" PARENT_SCOPE)
  # Canonical source for consumers and native tests: exact installed vcpkg WTL.
  set(_openatl_wtl_original_include "${_wtl_source}" PARENT_SCOPE)

  if(NOT OPENATL_WTL_FULL_ORIGINAL)
    message(FATAL_ERROR "Original vcpkg WTL is mandatory; no build-local adapter or alternate provider")
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_wtl_identity}" "${_wtl_source}/atlapp.h")
  target_include_directories(openatl_atl INTERFACE "${_wtl_source}")
  message(STATUS "OpenATL WTL: original vcpkg wtl@10.0.10320#7; no adapter")
endfunction()
_openatl_prepare_wtl()
