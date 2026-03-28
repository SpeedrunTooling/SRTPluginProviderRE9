#include "Process.h"
#include <Psapi.h>

namespace SRTPluginRE9::Process
{
	const std::expected<const std::string, std::string> GetProcessModulePathByNameA(HANDLE hProcess, const char moduleName[])
	{
		HMODULE hMods[1024];
		DWORD cbNeeded;
		unsigned int i;

		// Get a list of all the modules in this process.
		if (EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded))
		{
			DWORD szModNameSize = (MAX_PATH * sizeof(char)) + sizeof(char);
			std::string szModName(szModNameSize, 0);
			for (i = 0; i < (cbNeeded / sizeof(HMODULE)); i++)
			{
				// Get the full path to the module's file.
				if (GetModuleFileNameExA(hProcess, hMods[i], szModName.data(), szModNameSize))
				{
					if (_stricmp(szModName.data(), moduleName))
						return szModName;
				}
			}
		}

		return std::unexpected("Unable to find module with the supplied name");
	}

	const std::expected<const std::wstring, std::wstring> GetProcessModulePathByNameW(HANDLE hProcess, wchar_t moduleName[])
	{
		HMODULE hMods[1024];
		DWORD cbNeeded;
		unsigned int i;

		// Get a list of all the modules in this process.
		if (EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded))
		{
			DWORD szModNameSize = (MAX_PATH * sizeof(wchar_t)) + sizeof(wchar_t);
			std::wstring szModName(szModNameSize, 0);
			for (i = 0; i < (cbNeeded / sizeof(HMODULE)); i++)
			{
				// Get the full path to the module's file.
				if (GetModuleFileNameExW(hProcess, hMods[i], szModName.data(), szModNameSize))
				{
					if (_wcsicmp(szModName.data(), moduleName))
						return szModName;
				}
			}
		}

		return std::unexpected(L"Unable to find module with the supplied name");
	}
}
