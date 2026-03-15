#include "AOBScanner.h"

#include <algorithm>
#include <cctype>

namespace SRTPluginRE9::Hook
{
	std::pair<uintptr_t, uintptr_t> AOBScanner::GetModuleRange(const wchar_t *moduleName)
	{
		auto *base = reinterpret_cast<const uint8_t *>(GetModuleHandleW(moduleName));
		if (!base)
			return {0, 0};

		// The DOS header is at the base of the module.
		auto *dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
		if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE)
			return {0, 0};

		// The NT headers are at base + e_lfanew.
		auto *ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dosHeader->e_lfanew);
		if (ntHeaders->Signature != IMAGE_NT_SIGNATURE)
			return {0, 0};

		auto start = reinterpret_cast<uintptr_t>(base);
		auto end = start + ntHeaders->OptionalHeader.SizeOfImage;

		return {start, end};
	}

	std::vector<AOBScanner::PatternByte> AOBScanner::ParsePattern(const std::string_view pattern) // TODO: Maybe switch to std::expected to give better error response to caller...
	{
		std::vector<PatternByte> result;
		result.reserve(pattern.size() / 3 + 1);

		size_t i = 0;
		while (i < pattern.size())
		{
			// Skip whitespace.
			if (std::isspace(static_cast<unsigned char>(pattern[i])))
			{
				++i;
				continue;
			}

			// Need at least 2 characters for a byte token.
			if (i + 1 >= pattern.size())
				return {}; // Parse error: dangling nibble.

			char hi = pattern[i];
			char lo = pattern[i + 1];
			i += 2;

			PatternByte pb{};

			// High nibble
			if (hi == '?')
			{
				// High nibble is wild — mask out the upper 4 bits.
				pb.mask &= 0x0F;
			}
			else
			{
				auto n = HexCharToNibble(hi);
				if (n < 0)
					return {}; // Parse error: invalid hex char.
				pb.value |= static_cast<uint8_t>(n << 4);
			}

			// Low nibble
			if (lo == '?')
			{
				// Low nibble is wild — mask out the lower 4 bits.
				pb.mask &= 0xF0;
			}
			else
			{
				auto n = HexCharToNibble(lo);
				if (n < 0)
					return {}; // Parse error: invalid hex char.
				pb.value |= static_cast<uint8_t>(n);
			}

			result.push_back(pb);
		}

		return result;
	}

	std::vector<uintptr_t> AOBScanner::ScanMemory(
	    const std::string_view pattern,
	    uintptr_t startAddress,
	    uintptr_t endAddress)
	{
		auto parsed = ParsePattern(pattern);
		if (parsed.empty())
			return {};
		return ScanMemory(parsed, startAddress, endAddress);
	}

	std::vector<uintptr_t> AOBScanner::ScanMemory(
	    const std::vector<PatternByte> &pattern,
	    uintptr_t startAddress,
	    uintptr_t endAddress)
	{
		std::vector<uintptr_t> results;

		if (pattern.empty())
			return results;

		const size_t patternSize = pattern.size();

		// Find the first non-wildcard byte for a quick pre-filter.
		// Use memchr to skip large runs of non-matching bytes.
		size_t firstExactIdx = SIZE_MAX;
		uint8_t firstExactVal = 0;
		for (size_t i = 0; i < patternSize; ++i)
		{
			if (pattern[i].mask == 0xFF)
			{
				firstExactIdx = i;
				firstExactVal = pattern[i].value;
				break;
			}
		}

		// We may need to carry over bytes from the end of one region
		// to handle patterns that span page/region boundaries.
		std::vector<uint8_t> overlapBuffer;
		overlapBuffer.reserve(patternSize - 1);
		uintptr_t overlapBaseAddress = 0;

		auto address = startAddress;
		while (address < endAddress)
		{
			MEMORY_BASIC_INFORMATION mbi{};
			if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)) == 0)
				break;

			auto regionBase = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
			auto regionEnd = regionBase + mbi.RegionSize;

			// Advance past this region if it's not readable committed memory.
			if (mbi.State != MEM_COMMIT || !IsReadableProtection(mbi.Protect) ||
			    (mbi.Protect & PAGE_GUARD) != 0)
			{
				overlapBuffer.clear();
				address = regionEnd;
				continue;
			}

			auto *data = reinterpret_cast<const uint8_t *>(regionBase);
			auto dataSize = mbi.RegionSize;

			// Check for cross-boundary matches using the overlap buffer
			// from the previous region.
			if (!overlapBuffer.empty() && patternSize > 1)
			{
				// Append enough bytes from the start of this region.
				size_t needed = std::min(patternSize - 1, dataSize);
				size_t prevSize = overlapBuffer.size();
				overlapBuffer.insert(overlapBuffer.end(), data, data + needed);

				// Scan the overlap zone for matches
				for (size_t i = 0; i < prevSize && i + patternSize <= overlapBuffer.size(); ++i)
				{
					if (MatchAtOffset(overlapBuffer.data(), overlapBuffer.size(), i, pattern))
						results.push_back(overlapBaseAddress + i);
				}
			}

			// Scan this region
			size_t scanEnd = (dataSize >= patternSize) ? dataSize - patternSize + 1 : 0;

			if (firstExactIdx != SIZE_MAX && scanEnd > 0)
			{
				// Fast path: use memchr to find candidates for the first exact byte.
				size_t pos = 0;
				while (pos + firstExactIdx < dataSize &&
				       pos + patternSize <= dataSize)
				{
					// Search for the first exact byte starting at the expected offset.
					auto *found = static_cast<const uint8_t *>(
					    std::memchr(data + pos + firstExactIdx,
					                firstExactVal,
					                dataSize - pos - firstExactIdx));

					if (!found)
						break;

					size_t candidateStart = static_cast<size_t>(found - data) - firstExactIdx;

					// Bounds check.
					if (candidateStart + patternSize > dataSize)
						break;

					if (MatchAtOffset(data, dataSize, candidateStart, pattern))
						results.push_back(regionBase + candidateStart);

					pos = candidateStart + 1;
				}
			}
			else
			{
				// Slow path: all wildcards, check every position.
				for (size_t i = 0; i < scanEnd; ++i)
				{
					if (MatchAtOffset(data, dataSize, i, pattern))
						results.push_back(regionBase + i);
				}
			}

			// Save the tail of this region for cross-boundary detection.
			if (patternSize > 1)
			{
				size_t tailSize = std::min(patternSize - 1, dataSize);
				overlapBuffer.assign(data + dataSize - tailSize, data + dataSize);
				overlapBaseAddress = regionBase + dataSize - tailSize;
			}
			else
			{
				overlapBuffer.clear();
			}

			address = regionEnd;
		}

		return results;
	}
}
