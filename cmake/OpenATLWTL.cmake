# Repository-owned WTL 10.0.10320 GUI frontend. The original MS-PL
# sources, Resources, Samples and AppWizard are retained under wtl/.
# ATL/COM remains private to the openatl_atl backend; no second GUI provider.
option(OPENATL_WTL_FULL_ORIGINAL
  "Require the complete patched WTL10 source tree without an adapter" ON)
if(NOT OPENATL_WTL_FULL_ORIGINAL)
  message(FATAL_ERROR "Only complete repository-owned WTL 10.0.10320 is supported")
endif()
set(_wtl_root "${CMAKE_CURRENT_SOURCE_DIR}/wtl")
set(_wtl_source "${_wtl_root}/Include")
set(_openatl_wtl_samples "${_wtl_root}/Samples")
set(_openatl_wtl_original_include "${_wtl_source}")
foreach(_required IN ITEMS
    MS-PL.txt ReadMe.html SOURCE.md source-manifest.json AppWizard/Setup.js
    Include/atlapp.h Include/atlcrack.h Include/atlctrls.h
    Include/atlctrlw.h Include/atlctrlx.h Include/atlddx.h
    Include/atldlgs.h Include/atldwm.h Include/atlfind.h
    Include/atlframe.h Include/atlgdi.h Include/atlmisc.h
    Include/atlprint.h Include/atlres.h Include/atlribbon.h
    Include/atlscrl.h Include/atlsplit.h Include/atltheme.h
    Include/atluser.h Include/atlwinx.h
    Samples/GuidGen/GuidGen.cpp
    Samples/WTLExplorer/WTLExplorer.cpp
    Samples/Wizard97Test/Wizard97Test.cpp)
  if(NOT EXISTS "${_wtl_root}/${_required}")
    message(FATAL_ERROR "Missing repository-owned WTL release file: ${_required}")
  endif()
endforeach()
if(EXISTS "${_wtl_source}/atlbase.h" OR EXISTS "${_wtl_source}/atlwin.h")
  message(FATAL_ERROR "WTL frontend must not contain a second ATL backend")
endif()
target_include_directories(openwtl_wtl BEFORE INTERFACE "${_wtl_source}")
message(STATUS "OpenWTL::WTL: repository WTL 10.0.10320, patched, no vcpkg/alternate provider")
