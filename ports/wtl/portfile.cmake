# WTL 10.0.10320 overlay #8: SHA512-pinned SourceForge source is
# transformed using unique structural anchors; no mirror patch or
# registry/COM Server compatibility fallback.
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
)


# Transform the SHA512-verified SourceForge WTL10 source itself.
# Exact structural anchors: fail if source ownership changes.
set(_wtl_ui_header "${SOURCE_PATH}/Include/atlapp.h")
file(READ "${_wtl_ui_header}" _wtl_ui_text)
string(REPLACE "\r\n" "\n" _wtl_ui_text "${_wtl_ui_text}")

function(_openatl_replace_once needle value label)
    string(FIND "${_wtl_ui_text}" "${needle}" _begin)
    if(_begin EQUAL -1)
        message(FATAL_ERROR "WTL UI-only missing ${label}")
    endif()
    string(LENGTH "${needle}" _length)
    math(EXPR _after "${_begin} + ${_length}")
    string(SUBSTRING "${_wtl_ui_text}" ${_after} -1 _tail)
    string(FIND "${_tail}" "${needle}" _repeat)
    if(NOT _repeat EQUAL -1)
        message(FATAL_ERROR "WTL UI-only duplicate ${label}")
    endif()
    string(SUBSTRING "${_wtl_ui_text}" 0 ${_begin} _prefix)
    set(_wtl_ui_text "${_prefix}${value}${_tail}" PARENT_SCOPE)
endfunction()

function(_openatl_replace_between start_marker end_marker value label)
    string(FIND "${_wtl_ui_text}" "${start_marker}" _begin)
    if(_begin EQUAL -1)
        message(FATAL_ERROR "WTL UI-only missing start ${label}")
    endif()
    string(LENGTH "${start_marker}" _size)
    math(EXPR _after "${_begin} + ${_size}")
    string(SUBSTRING "${_wtl_ui_text}" ${_after} -1 _tail)
    string(FIND "${_tail}" "${start_marker}" _duplicate)
    if(NOT _duplicate EQUAL -1)
        message(FATAL_ERROR "WTL UI-only duplicate start ${label}")
    endif()
    string(FIND "${_tail}" "${end_marker}" _end_rel)
    if(_end_rel EQUAL -1)
        message(FATAL_ERROR "WTL UI-only missing end ${label}")
    endif()
    math(EXPR _end "${_after} + ${_end_rel}")
    string(SUBSTRING "${_wtl_ui_text}" 0 ${_begin} _prefix)
    string(SUBSTRING "${_wtl_ui_text}" ${_end} -1 _suffix)
    set(_wtl_ui_text "${_prefix}${value}${_suffix}" PARENT_SCOPE)
endfunction()

_openatl_replace_once(
    "ATL::_pAtlModule->m_csStaticDataInitAndTypeInfo"
    "ATL::_AtlWinModule.m_csWindowCreate"
    "Win32 UI critical-section owner")
_openatl_replace_between(
    "class CAppModule : public ATL::CComModule"
    "// Message loop map methods"
    [==[
class CAppModule
{
public:
	DWORD m_dwMainThreadID = 0;
	ATL::CSimpleMap<DWORD, CMessageLoop*>* m_pMsgLoopMap = NULL;
	ATL::CSimpleArray<HWND>* m_pSettingChangeNotify = NULL;

	// OpenATL UI-only: real Win32 module lifecycle, no COM object map.
	HRESULT Init(HINSTANCE hInstance)
	{
		if(hInstance == NULL)
			return E_INVALIDARG;
		if(m_pMsgLoopMap != NULL)
			return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);

		typedef ATL::CSimpleMap<DWORD, CMessageLoop*> _mapClass;
		_mapClass* pMap = NULL;
		ATLTRY(pMap = new _mapClass);
		if(pMap == NULL)
			return E_OUTOFMEMORY;

		m_dwMainThreadID = ::GetCurrentThreadId();
		m_pMsgLoopMap = pMap;
		m_pSettingChangeNotify = NULL;
		ATL::_AtlBaseModule.m_hInst = hInstance;
		ATL::_AtlBaseModule.m_hInstResource = hInstance;
		return S_OK;
	}

	void Term()
	{
		if(m_pMsgLoopMap != NULL)
		{
			TermSettingChangeNotify();
			delete m_pMsgLoopMap;
			m_pMsgLoopMap = NULL;
		}
	}

	HINSTANCE GetModuleInstance() const
	{
		return ATL::_AtlBaseModule.GetModuleInstance();
	}

	HINSTANCE GetResourceInstance() const
	{
		return ATL::_AtlBaseModule.GetResourceInstance();
	}

]==]
    "CAppModule lifecycle")
_openatl_replace_between(
    "// CServerAppModule - module class for a COM server application"
    "typedef ATL::CRegKey CRegKeyEx;"
    ""
    "COM Server class and legacy registration block")
_openatl_replace_once(
    "typedef ATL::CRegKey CRegKeyEx;"
    ""
    "retired CRegKeyEx alias")
if(NOT _wtl_ui_text MATCHES "HRESULT Init\\(HINSTANCE hInstance\\)" OR
   _wtl_ui_text MATCHES "class CAppModule : public ATL::CComModule" OR
   _wtl_ui_text MATCHES "class CServerAppModule" OR
   _wtl_ui_text MATCHES "typedef ATL::CRegKey CRegKeyEx" OR
   _wtl_ui_text MATCHES "ATL::_pAtlModule")
    message(FATAL_ERROR "WTL UI-only source still exposes COM Server")
endif()
file(WRITE "${_wtl_ui_header}" "${_wtl_ui_text}")
unset(_wtl_ui_header)
unset(_wtl_ui_text)

file(INSTALL "${SOURCE_PATH}/Include/" DESTINATION "${CURRENT_PACKAGES_DIR}/include" FILES_MATCHING PATTERN "*.h")
file(COPY "${SOURCE_PATH}/Samples" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
file(COPY "${SOURCE_PATH}/AppWizard" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
file(INSTALL "${SOURCE_PATH}/MS-PL.txt" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
