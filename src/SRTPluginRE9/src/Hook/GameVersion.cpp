#include "GameVersion.h"
#include "Globals.h"
#include "Hash.h"
#include "Process.h"
#include <array>

namespace SRTPluginRE9::GameVersion
{
	constinit std::array<uint8_t, 32> WW_20260225_1 = {0x44, 0x04, 0x4E, 0xCF, 0xB7, 0x15, 0x28, 0x26, 0x32, 0x05, 0x86, 0xE5, 0xCC, 0x40, 0xFE, 0x8C, 0xEE, 0xB2, 0x77, 0x2C, 0x58, 0x07, 0x98, 0x51, 0x13, 0xA5, 0x90, 0x4B, 0xFD, 0xF3, 0x58, 0xC7};
	constinit std::array<uint8_t, 32> WW_20260327_1 = {0xA0, 0x12, 0x51, 0x45, 0x3A, 0x8F, 0x08, 0x63, 0x90, 0x6C, 0x04, 0xE0, 0x86, 0x7A, 0x92, 0x11, 0x69, 0x63, 0x9F, 0xC8, 0xA1, 0xA4, 0xD7, 0x73, 0x6D, 0x73, 0xBA, 0x7C, 0xA4, 0x91, 0x46, 0x82};

	const std::expected<const GameVersion, std::string> DetectGameVersion()
	{
		auto gameExePath = SRTPluginRE9::Process::GetProcessModulePathByNameA(GetCurrentProcess(), "re9.exe");
		if (!gameExePath.has_value())
			return std::unexpected(gameExePath.error());

		auto gameExeHash = SRTPluginRE9::Hash::GetFileHashSHA256(gameExePath.value());
		if (!gameExeHash.has_value())
			return std::unexpected(gameExeHash.error());

		logger->LogMessage("GameVersion::DetectGameVersion() Game SHA256 Checksum: {}\n", SRTPluginRE9::Hash::ArrayToHexString(gameExeHash.value()).c_str());
		if (gameExeHash.value() == WW_20260327_1)
			return GameVersion::WW_20260327_1;
		else if (gameExeHash.value() == WW_20260225_1)
			return GameVersion::WW_20260225_1;
		else
			return GameVersion::Unknown;
	}
}
