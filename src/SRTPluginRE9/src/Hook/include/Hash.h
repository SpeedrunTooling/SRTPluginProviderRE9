#ifndef SRTPLUGINRE9_HASH_H
#define SRTPLUGINRE9_HASH_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <array>
#include <expected>
#include <memory>
#include <string>

namespace SRTPluginRE9::Hash
{
	const std::expected<const std::array<uint8_t, 32>, std::string> GetFileHashSHA256(const char *);

	const std::string ArrayToHexString(const std::array<uint8_t, 32> &);
}

#endif