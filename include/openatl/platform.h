// OpenATL's Windows 7 minimum also applies to legacy sample version defines.
#ifndef OPENATL_PLATFORM_H
#define OPENATL_PLATFORM_H
#if !defined(WINVER) || WINVER < 0x0601
#undef WINVER
#define WINVER 0x0601
#endif
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0601
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef _WIN32_IE
#define _WIN32_IE 0x0800
#endif
#endif
