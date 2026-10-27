# WTL 10.0.10320 overlay #7: the original source SHA512 and maintenance
# patches are retained. The WTL UI module is made independent of
# Microsoft ATL's COM Server, class maps and registry writers.
# Difference: our overlay removes the stock WTL -> atl dependency;
# stock vcpkg's 'atl' port requires Microsoft Visual Studio ATL.
# OpenATL's source-tree include/ is the ONLY ATL implementation.
vcpkg_from_sourceforge(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO wtl/WTL%2010
    REF WTL%2010.0.10320%20Release
    FILENAME "WTL10_10320_Release.zip"
    NO_REMOVE_ONE_LEVEL
    SHA512 086a6cf6a49a4318a8c519136ba6019ded7aa7f2c1d85f78c30b21183654537b3428a400a64fcdacba3a7a10a9ef05137b6f2119f59594da300d55f9ebfb1309
    PATCHES
        appwizard_setup.js-vs2022.patch
        atlmisc.h-bug329.patch
        atlribbon.h-wtl66.patch
        atlapp.h-openatl-ui-only.patch
)

file(INSTALL "${SOURCE_PATH}/Include/" DESTINATION "${CURRENT_PACKAGES_DIR}/include" FILES_MATCHING PATTERN "*.h")
file(COPY "${SOURCE_PATH}/Samples" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
file(COPY "${SOURCE_PATH}/AppWizard" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
file(INSTALL "${SOURCE_PATH}/MS-PL.txt" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
