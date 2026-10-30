# Original sources and resources from the checksum-pinned WTL release.
# No source rewriting or manual ATL/WTL include roots.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
# Full coverage uses the upstream Win32 project configuration. In particular,
# MTPad's EDITSTREAM callback and Wizard97Test's return types are 32-bit APIs.
if(OPENATL_BUILD_OFFICIAL_SUITE)
  set(samples Aero Alpha BmpView GuidGen MDIDocVw MemDlg MTPad MTPad7 TabBrowser Wizard97Test WTLExplorer)
else()
  set(samples GuidGen)
  if(MSVC)
    list(APPEND samples Aero Alpha BmpView MDIDocVw MemDlg WTLExplorer)
  endif()
endif()
set(Aero_sources Aero.cpp stdafx.cpp)
set(Alpha_sources Alpha.cpp stdafx.cpp)
set(BmpView_sources BmpView.cpp stdafx.cpp)
set(GuidGen_sources GuidGen.cpp stdatl.cpp)
set(MDIDocVw_sources MDI.cpp stdafx.cpp)
set(MemDlg_sources MemDlg.cpp stdafx.cpp)
set(MTPad_sources MTPad.cpp stdatl.cpp)
set(MTPad7_sources MTPad.cpp stdatl.cpp)
set(TabBrowser_sources stdafx.cpp TabBrowser.cpp)
set(Wizard97Test_sources stdafx.cpp Wizard97Test.cpp
  Wizard/TestWizard.cpp Wizard/TestWizardCompletionPage.cpp Wizard/TestWizardFilePreviewPage.cpp
  Wizard/TestWizardInfo.cpp Wizard/TestWizardOutputPage.cpp Wizard/TestWizardPathFilterPage.cpp
  Wizard/TestWizardSheet.cpp Wizard/TestWizardWelcomePage.cpp)
set(WTLExplorer_sources MainFrm.Cpp ShellMgr.Cpp stdafx.cpp WTLExplorer.cpp)
set(MDIDocVw_resource MDI.rc)
set(MTPad_resource MTPad.rc)
set(MTPad7_resource mtpad.rc)
foreach(sample IN LISTS samples)
  string(TOLOWER "${sample}" name)
  set(target "openatl_official_${name}")
  set(source "${_openatl_wtl_samples}/${sample}")
  set(sources)
  foreach(file IN LISTS ${sample}_sources)
    list(APPEND sources "${source}/${file}")
  endforeach()
  # The upstream archive uses .Cpp for two WTLExplorer translation units.
  set_source_files_properties(${sources} PROPERTIES LANGUAGE CXX)
  if(DEFINED ${sample}_resource)
    set(resource "${${sample}_resource}")
  else()
    set(resource "${sample}.rc")
  endif()
  add_executable(${target} WIN32 ${sources} "${source}/${resource}")
  if(NOT CMAKE_HOST_WIN32 AND sample STREQUAL "GuidGen")
    # llvm-rc opens resource filenames verbatim on POSIX, including the doubled
    # backslash in the original .rc. Supply binary aliases without editing it.
    set(resource_paths "${CMAKE_CURRENT_BINARY_DIR}/${name}-resources")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
      "${source}/res/GuidGen.ico")
    execute_process(COMMAND "${Python3_EXECUTABLE}"
      "${PROJECT_SOURCE_DIR}/cmake/ResourcePaths.py"
      "${source}/res/GuidGen.ico" "res/GuidGen.ico" "${resource_paths}"
      COMMAND_ERROR_IS_FATAL ANY)
    target_include_directories(${target} PRIVATE "${resource_paths}")
  endif()
  if(sample STREQUAL "MDIDocVw")
    set_target_properties(${target} PROPERTIES OPENATL_USE_WINMAIN TRUE)
  endif()
  if(sample STREQUAL "MTPad7")
    find_program(OPENATL_UICC_EXECUTABLE NAMES uicc.exe
      HINTS "$ENV{WindowsSdkDir}/bin/$ENV{WindowsSDKVersion}/x86"
        "$ENV{OPENATL_WINDOWS_SDK_BIN}" REQUIRED)
    set(ribbon "${CMAKE_CURRENT_BINARY_DIR}/MTPadRibbon.bml")
    add_custom_command(OUTPUT "${ribbon}"
      COMMAND "${OPENATL_UICC_EXECUTABLE}" "${source}/MTPadRibbon.xml" "${ribbon}"
      DEPENDS "${source}/MTPadRibbon.xml" VERBATIM)
    target_sources(${target} PRIVATE "${ribbon}")
    set_source_files_properties("${source}/${resource}" PROPERTIES OBJECT_DEPENDS "${ribbon}")
    # The archive already contains UICC's .h/.rc; generate only their BML input.
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
  endif()
  target_link_libraries(${target} PRIVATE OpenWTL::WTL)
  if(sample STREQUAL "Wizard97Test")
    target_link_libraries(${target} PRIVATE htmlhelp)
    if(MSVC)
      target_compile_options(${target} PRIVATE /source-charset:.1252)
    elseif(MINGW)
      # Its wide literals contain CP1252 bullets. Decode compiler input as
      # MSVC does, keeping the archived source bytes and C++ text unchanged.
      set_property(TARGET ${target} PROPERTY CXX_COMPILER_LAUNCHER
        "${Python3_EXECUTABLE};${PROJECT_SOURCE_DIR}/cmake/CompilerInput.py;--source;${source}/Wizard/TestWizardCompletionPage.cpp;--charset;cp1252;--")
    endif()
  endif()
  target_include_directories(${target} PRIVATE "${source}")
  target_compile_definitions(${target} PRIVATE NDEBUG)
  set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
  if(MSVC)
    target_compile_options(${target} PRIVATE /W3 /EHsc)
    target_link_options(${target} PRIVATE /INCREMENTAL:NO /MANIFEST:EMBED
      "/MANIFESTINPUT:${PROJECT_SOURCE_DIR}/tests/wtl10/sdi_comctl6.manifest")
  else()
    # The unchanged MSVC examples use dependent-base lookup and other language
    # extensions. Keep Clang's GCC compatibility macros for the MinGW headers.
    target_compile_options(${target} PRIVATE -fms-extensions -fms-compatibility
      -fgnuc-version=4.2.1 -fms-compatibility-version=19.44 -U_MSC_VER -fdelayed-template-parsing)
    if(sample STREQUAL "MDIDocVw")
      # The original project privately inherits CUpdateUI, then its WTL base
      # downcasts through that private base. MSVC accepts this legacy pattern.
      target_compile_options(${target} PRIVATE -fno-access-control)
    endif()
    target_link_options(${target} PRIVATE -static)
    set(manifest "${CMAKE_CURRENT_BINARY_DIR}/${name}.rc")
    file(WRITE "${manifest}"
      "1 24 \"${PROJECT_SOURCE_DIR}/tests/wtl10/sdi_comctl6.manifest\"\n")
    target_sources(${target} PRIVATE "${manifest}")
  endif()
  # Native behavior coverage is separate from whole-suite build coverage.
  if(sample STREQUAL "GuidGen" OR sample STREQUAL "MemDlg")
    add_test(NAME "openatl.official_${name}" COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/wtl_interactive.py" "${name}" "$<TARGET_FILE:${target}>")
    set_tests_properties("openatl.official_${name}" PROPERTIES TIMEOUT 35
      RESOURCE_LOCK openatl_official_wtl_ui)
  endif()
endforeach()
