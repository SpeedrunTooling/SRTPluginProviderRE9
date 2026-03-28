#ifndef SRTPLUGINRE9_GAMEVERSION_H
#define SRTPLUGINRE9_GAMEVERSION_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <expected>
#include <string>

namespace SRTPluginRE9::GameVersion
{
	enum class GameVersion
	{
		Unknown = 0,
		WW_20260225_1 = 1,
		WW_20260327_1 = 2,
	};

	const std::expected<const GameVersion, std::string> DetectGameVersion();
}

#endif