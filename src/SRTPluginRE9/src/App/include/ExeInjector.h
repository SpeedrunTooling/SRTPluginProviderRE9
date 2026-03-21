#ifndef SRTPLUGINRE9_EXEINJECTOR_H
#define SRTPLUGINRE9_EXEINJECTOR_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <filesystem>
#include <minwindef.h>
#include <string>
#include <windows.h>

#ifndef TSTRING
#ifdef UNICODE
#define TSTRING std::wstring
#else
#define TSTRING std::string
#endif
#endif

#ifndef TSTRICMP
#ifdef UNICODE
#define TSTRICMP _wcsicmp
#else
#define TSTRICMP _stricmp
#endif
#endif

#ifndef TSTRCMP
#ifdef UNICODE
#define TSTRCMP wcscmp
#else
#define TSTRCMP strcmp
#endif
#endif

#ifndef GetStringSize
#ifdef UNICODE
#define GetStringSize GetStringSizeW
#else
#define GetStringSize GetStringSizeA
#endif
#endif

DWORD GetProcessIdByName(const TCHAR *name);
size_t GetStringSizeA(const std::string &string);
size_t GetStringSizeW(const std::wstring &string);

#endif
