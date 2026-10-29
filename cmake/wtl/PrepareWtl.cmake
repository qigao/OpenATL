# Transform the SHA512-verified SourceForge WTL10 source itself.
# Exact structural anchors: fail if source ownership changes.
set(_wtl_ui_header "${_wtl_headers}/atlapp.h")
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

	// Source compatibility for WTL's ordinary empty object-map call.
	HRESULT Init(void* objectMap, HINSTANCE hInstance, const GUID* libraryId = NULL)
	{
		if(objectMap != NULL || libraryId != NULL)
			return E_NOTIMPL;
		return Init(hInstance);
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
if(NOT _wtl_ui_text MATCHES "HRESULT Init\\(HINSTANCE hInstance\\)" OR
   _wtl_ui_text MATCHES "class CAppModule : public ATL::CComModule" OR
   _wtl_ui_text MATCHES "class CServerAppModule" OR
   _wtl_ui_text MATCHES "ATL::_pAtlModule")
    message(FATAL_ERROR "WTL UI-only source still exposes COM Server")
endif()
file(WRITE "${_wtl_ui_header}" "${_wtl_ui_text}")

# The Microsoft compiler accepts a CWindow -> HWND -> CButton copy
# initialization used by BmpView. Supply that conversion directly on Clang;
# preserve every existing constructor and the public HWND representation.
set(_wtl_ui_header "${_wtl_headers}/atlctrls.h")
file(READ "${_wtl_ui_header}" _wtl_ui_text)
foreach(_control IN ITEMS CButtonT CEditT)
  _openatl_replace_once(
    "${_control}(HWND hWnd = NULL) : TBase(hWnd)"
    "${_control}(const ATL::CWindow& window) : TBase(window.m_hWnd) { }\n\t${_control}& operator=(const ATL::CWindow& window) { this->m_hWnd = window.m_hWnd; return *this; }\n\t${_control}(HWND hWnd = NULL) : TBase(hWnd)"
    "${_control} CWindow copy initialization")
endforeach()
file(WRITE "${_wtl_ui_header}" "${_wtl_ui_text}")

# mingw-w64's GetThemePartSize declares the SDK input RECT mutable. Pass a
# local copy while retaining WTL's standard const RECT public signature.
set(_wtl_ui_header "${_wtl_headers}/atltheme.h")
file(READ "${_wtl_ui_header}" _wtl_ui_text)
_openatl_replace_once(
    "return ::GetThemePartSize(m_hTheme, hDC, nPartID, nStateID, pRect, eSize, pSize);"
    "RECT copy = {};\n\t\tif(pRect != NULL) copy = *pRect;\n\t\treturn ::GetThemePartSize(m_hTheme, hDC, nPartID, nStateID, pRect != NULL ? &copy : NULL, eSize, pSize);"
    "MinGW SDK GetThemePartSize input rectangle")
file(WRITE "${_wtl_ui_header}" "${_wtl_ui_text}")

# Retain upstream WTL's CHyperLink registry color lookup and string parser.
# OpenATL::CRegKey owns the Win32 key; this section is no longer deleted.
set(_wtl_ui_header "${_wtl_headers}/atlctrlx.h")
file(READ "${_wtl_ui_header}" _wtl_ui_text)
foreach(_token IN ITEMS "ATL::CRegKey" "_ParseColorString"
                         "Anchor Color" "Anchor Color Visited")
  string(FIND "${_wtl_ui_text}" "${_token}" _found)
  if(_found EQUAL -1)
    message(FATAL_ERROR "WTL original hyperlink color query missing: ${_token}")
  endif()
endforeach()

# The list sorter uses a CRTP enum as an array bound, as the command bar does.
set(_wtl_ui_header "${_wtl_headers}/atlctrlx.h")
file(READ "${_wtl_ui_header}" _wtl_ui_text)
_openatl_replace_once("pszTemp[pT->m_cchCmpTextMax]" "pszTemp[T::m_cchCmpTextMax]"
  "list sorter compile-time text capacity")
file(WRITE "${_wtl_ui_header}" "${_wtl_ui_text}")

# Restore upstream WTL PrintDlgEx with OpenATL's actual IObjectWithSiteImpl
# and COM Client runtime; do not delete IPrintDialogCallback public APIs.
set(_wtl_ui_header "${_wtl_headers}/atldlgs.h")
file(READ "${_wtl_ui_header}" _wtl_ui_text)
foreach(_token IN ITEMS "#include <atlcom.h>"
                         "class ATL_NO_VTABLE CPrintDialogExImpl"
                         "public ATL::IObjectWithSiteImpl"
                         "class CPrintDialogEx" "class CPageSetupDialog")
  string(FIND "${_wtl_ui_text}" "${_token}" _found)
  if(_found EQUAL -1)
    message(FATAL_ERROR "WTL original PrintDlgEx callback missing: ${_token}")
  endif()
endforeach()

# Preserve MRU menu operations and per-user registry persistence.
# These are ordinary app settings via OpenATL::CRegKey, not COM registration.
set(_wtl_ui_header "${_wtl_headers}/atlmisc.h")
file(READ "${_wtl_ui_header}" _wtl_ui_text)
string(REPLACE "\r\n" "\n" _wtl_ui_text "${_wtl_ui_text}")
# Keep the complete upstream CRegProperty and CRegPropertyImpl<T>
# serializer, CRTP property map, binary/font/Unicode operations.
# Existing MRU and CFindFile public APIs remain unchanged.
foreach(_token IN ITEMS "class CRegProperty" "class CRegPropertyImpl"
                         "#define BEGIN_REGPROP_MAP"
                         "#define REG_PROPERTY" "#define END_REGPROP_MAP"
                         "ReadFromRegistry(" "WriteToRegistry("
                         "ATL::CRegKey" "CRecentDocumentListBase"
                         "CFindFile" "AtlGetStockPen")
  string(FIND "${_wtl_ui_text}" "${_token}" _found)
  if(_found EQUAL -1)
    message(FATAL_ERROR "WTL original registry property map missing: ${_token}")
  endif()
endforeach()
# Official bug329 WTL patch already applied by the pinned vcpkg overlay.
string(FIND "${_wtl_ui_text}" "ATL::CString strResult(_T(\"file://\"));" _wtl_bug329)
if(_wtl_bug329 EQUAL -1)
  message(FATAL_ERROR "WTL vcpkg bug329 patch missing")
endif()
file(WRITE "${_wtl_ui_header}" "${_wtl_ui_text}")

set(_wtl_ui_header "${_wtl_headers}/atlribbon.h")
file(READ "${_wtl_ui_header}" _wtl_ui_text)
string(REPLACE "\r\n" "\n" _wtl_ui_text "${_wtl_ui_text}")
# Official issue66 patch already applied by the pinned vcpkg overlay.
foreach(_header IN ITEMS atlmisc.h atlframe.h atlctrls.h atlctrlw.h)
  string(FIND "${_wtl_ui_text}" "#include \"${_header}\"" _wtl_issue66)
  if(_wtl_issue66 EQUAL -1)
    message(FATAL_ERROR "WTL vcpkg issue66 patch missing: ${_header}")
  endif()
endforeach()
foreach(_method IN ITEMS UIAddElement UIRemoveElement)
  string(REPLACE "this->${_method}<" "this->template ${_method}<" _wtl_ui_text "${_wtl_ui_text}")
endforeach()
_openatl_replace_once("return ::VarI4FromDec(pDecimal, &val);"
  "DECIMAL copy = *pDecimal;\n\t\treturn ::VarI4FromDec(&copy, &val);"
  "MinGW SDK input-only decimal")
file(WRITE "${_wtl_ui_header}" "${_wtl_ui_text}")

unset(_wtl_ui_header)
unset(_wtl_ui_text)

# Express CRTP enum bounds through the type: pT->enum is not a constant
# expression for Clang when it appears in an initialized array declaration.
set(_wtl_ui_header "${_wtl_headers}/atlctrlw.h")
file(READ "${_wtl_ui_header}" _wtl_ui_text)
string(REGEX MATCHALL "szString\\[pT->_nMaxMenuItemTextLength\\]" _wtl_bounds "${_wtl_ui_text}")
list(LENGTH _wtl_bounds _wtl_bound_count)
if(NOT _wtl_bound_count EQUAL 3)
  message(FATAL_ERROR "WTL command-bar constant-bound source changed")
endif()
string(REPLACE "szString[pT->_nMaxMenuItemTextLength]" "szString[T::_nMaxMenuItemTextLength]"
  _wtl_ui_text "${_wtl_ui_text}")
file(WRITE "${_wtl_ui_header}" "${_wtl_ui_text}")
