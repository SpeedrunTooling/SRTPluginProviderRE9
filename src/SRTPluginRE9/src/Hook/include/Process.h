#ifndef SRTPLUGINRE9_PROCESS_H
#define SRTPLUGINRE9_PROCESS_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <expected>
#include <string>
#include <windows.h>

#ifndef GetProcessModulePathByName
#ifdef UNICODE
#define GetProcessModulePathByName GetProcessModulePathByNameW
#else
#define GetProcessModulePathByName GetProcessModulePathByNameA
#endif
#endif

namespace SRTPluginRE9::Process
{
	const std::expected<const std::string, std::string> GetProcessModulePathByNameA(HANDLE hProcess, const char moduleName[]);
	const std::expected<const std::wstring, std::wstring> GetProcessModulePathByNameW(HANDLE hProcess, const wchar_t moduleName[]);
}

#endif