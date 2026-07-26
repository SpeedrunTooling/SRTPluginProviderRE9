#include "Process.h"
#include <Psapi.h>
#include <string.h>

namespace SRTPluginRE9::Process
{
	namespace
	{
		constexpr size_t MaxModules = 1024;

		// GetModuleFileNameEx truncates at the buffer size rather than failing, so a path
		// longer than MAX_PATH needs a second pass with room for a Windows long path.
		constexpr size_t LongPathLength = 32767;

		// Callers pass a bare module name ("re9.exe") but the enumerated value is a full
		// path, so the comparison has to be against the last path component.
		template <typename CharT>
		const CharT *BaseName(const CharT *path) noexcept
		{
			const CharT *leaf = path;
			for (const CharT *scan = path; *scan != CharT{}; ++scan)
				if (*scan == static_cast<CharT>('\\') || *scan == static_cast<CharT>('/'))
					leaf = scan + 1;
			return leaf;
		}
	}

	const std::expected<const std::string, std::string> GetProcessModulePathByNameA(HANDLE hProcess, const char moduleName[])
	{
		HMODULE hMods[MaxModules];
		DWORD cbNeeded;

		if (!EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded))
			return std::unexpected("Unable to enumerate the process' modules");

		// cbNeeded reports what was required, which can exceed what we had room for.
		const size_t reported = cbNeeded / sizeof(HMODULE);
		const size_t moduleCount = (reported < MaxModules) ? reported : MaxModules;

		for (size_t i = 0; i < moduleCount; ++i)
		{
			std::string modulePath(MAX_PATH, '\0');
			DWORD length = GetModuleFileNameExA(hProcess, hMods[i], modulePath.data(), static_cast<DWORD>(modulePath.size()));

			// Filling the buffer exactly means the path was truncated; retry with more room.
			if (length >= modulePath.size())
			{
				modulePath.assign(LongPathLength, '\0');
				length = GetModuleFileNameExA(hProcess, hMods[i], modulePath.data(), static_cast<DWORD>(modulePath.size()));
			}

			if (length == 0 || length >= modulePath.size())
				continue;

			// GetModuleFileNameEx writes through data() without touching the string's length,
			// so trim to what it actually wrote — otherwise the result carries trailing NULs
			// and every comparison, concatenation or log of it is wrong.
			modulePath.resize(length);

			if (_stricmp(BaseName(modulePath.c_str()), moduleName) == 0)
				return modulePath;
		}

		return std::unexpected("Unable to find module with the supplied name");
	}

	const std::expected<const std::wstring, std::wstring> GetProcessModulePathByNameW(HANDLE hProcess, const wchar_t moduleName[])
	{
		HMODULE hMods[MaxModules];
		DWORD cbNeeded;

		if (!EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded))
			return std::unexpected(L"Unable to enumerate the process' modules");

		const size_t reported = cbNeeded / sizeof(HMODULE);
		const size_t moduleCount = (reported < MaxModules) ? reported : MaxModules;

		for (size_t i = 0; i < moduleCount; ++i)
		{
			// Note nSize is a character count, not a byte count.
			std::wstring modulePath(MAX_PATH, L'\0');
			DWORD length = GetModuleFileNameExW(hProcess, hMods[i], modulePath.data(), static_cast<DWORD>(modulePath.size()));

			if (length >= modulePath.size())
			{
				modulePath.assign(LongPathLength, L'\0');
				length = GetModuleFileNameExW(hProcess, hMods[i], modulePath.data(), static_cast<DWORD>(modulePath.size()));
			}

			if (length == 0 || length >= modulePath.size())
				continue;

			modulePath.resize(length);

			if (_wcsicmp(BaseName(modulePath.c_str()), moduleName) == 0)
				return modulePath;
		}

		return std::unexpected(L"Unable to find module with the supplied name");
	}
}
