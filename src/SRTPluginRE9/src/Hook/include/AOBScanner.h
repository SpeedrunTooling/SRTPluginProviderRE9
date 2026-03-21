#ifndef SRTPLUGINRE9_AOBSCANNER_H
#define SRTPLUGINRE9_AOBSCANNER_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cstdint>
#include <string_view>
#include <vector>
#include <windows.h>

namespace SRTPluginRE9::Hook
{
	/*
// Restrict search range to a specific module.
auto base = get_module_base(L"re9.exe");
if (base.has_value())
{
	auto results = AOBScanner::ScanMemory("48 8B 05 ?? ?? ?? ??", base.value(), base.value() + 0x2000000);
}

// Restrict search range to the start and end of the exe in memory.
auto [exeStart, exeEnd] = AOBScanner::GetModuleRange(nullptr); // Only scan where the exe is loaded in memory.
auto results = AOBScanner::ScanMemory("0E 4C 8D B4 24 00 01 00 00 ?C 89 ?? 4D 89 F8 C5", exeStart, exeEnd);

// Pre-parse for repeated scans with the same pattern.
auto pattern = AOBScanner::ParsePattern("FD EB 1A ?? ?? 09 0A ?? F1 CC");
auto results1 = AOBScanner::ScanMemory(pattern);
// ...
auto results2 = AOBScanner::ScanMemory(pattern); // Re-use without re-parsing... RE!
	*/
	class AOBScanner
	{
	public:
		struct PatternByte
		{
			uint8_t value = 0x00; // The expected byte value (for non-wild nibbles).
			uint8_t mask = 0xFF;  // 0xFF = exact, 0xF0 = high fixed/low wild, 0x0F = low fixed/high wild, 0x00 = full wildcard.

			[[nodiscard]] bool Matches(uint8_t b) const noexcept
			{
				return (b & mask) == (value & mask);
			}
		};

		/// @brief Gets the memory range (base, base+size) of a loaded module.
		/// @param moduleName The module name (e.g. L"re9.exe"), or nullptr for the main executable.
		/// @return {base, end} pair, or {0, 0} if the module isn't loaded.
		[[nodiscard]] static std::pair<uintptr_t, uintptr_t> GetModuleRange(const wchar_t *moduleName = nullptr);

		/// @brief Parses an AOB pattern string into a vector of PatternBytes.
		/// Example: "FD EB 1A ?? ?? 09 0A ?? F1 CC" (wildcards) or "F? ?9 A0" (partial wildcards).
		/// @param pattern Space-separated hex string with '?' wildcards.
		/// @return Parsed pattern bytes, or empty vector on parse error.
		[[nodiscard]] static std::vector<PatternByte> ParsePattern(
		    const std::string_view pattern);

		/// @brief Scans process memory for all occurrences of an AOB pattern.
		/// Searches all committed, readable memory pages in the current process.
		/// Handles patterns that span page boundaries.
		/// @param pattern Parsed pattern bytes from ParsePattern().
		/// @param startAddress Start of the search range (default: 0x10000).
		/// @param endAddress End of the search range (default: 0x7FFFFFFFFFFF for 64-bit).
		/// @return Vector of addresses where the pattern was found.
		[[nodiscard]] static std::vector<uintptr_t> ScanMemory(
		    const std::vector<PatternByte> &pattern,
		    uintptr_t startAddress = 0x10000,
		    uintptr_t endAddress = 0x7FFFFFFFFFFF);

		/// @brief Convenience overload: parses the pattern string and scans in one call.
		/// @param pattern Space-separated hex string with '?' wildcards.
		/// @param startAddress Start of the search range.
		/// @param endAddress End of the search range.
		/// @return Vector of addresses where the pattern was found.
		[[nodiscard]] static std::vector<uintptr_t> ScanMemory(
		    const std::string_view pattern,
		    uintptr_t startAddress = 0x10000,
		    uintptr_t endAddress = 0x7FFFFFFFFFFF);

	private:
		static int8_t HexCharToNibble(const char c) noexcept
		{
			if (c >= '0' && c <= '9')
				return c - '0';
			if (c >= 'A' && c <= 'F')
				return c - 'A' + 10;
			if (c >= 'a' && c <= 'f')
				return c - 'a' + 10;
			return -1;
		}

		static bool IsReadableProtection(DWORD protect) noexcept
		{
			switch (protect & 0xFF)
			{
				case PAGE_READONLY:
				case PAGE_READWRITE:
				case PAGE_WRITECOPY:
				case PAGE_EXECUTE_READ:
				case PAGE_EXECUTE_READWRITE:
				case PAGE_EXECUTE_WRITECOPY:
					return true;
				default:
					return false;
			}
		}

		static bool MatchAtOffset(
		    const uint8_t *data,
		    size_t dataSize,
		    size_t offset,
		    const std::vector<AOBScanner::PatternByte> &pattern) noexcept
		{
			if (offset + pattern.size() > dataSize)
				return false;

			for (size_t j = 0; j < pattern.size(); ++j)
			{
				if (!pattern[j].Matches(data[offset + j]))
					return false;
			}

			return true;
		}
	};
}

#endif
