#ifndef SRTPLUGINRE9_DATETIME_H
#define SRTPLUGINRE9_DATETIME_H

#include <chrono>

#define SRT_DATE_FORMAT "%F"
#define SRT_TIME_FORMAT "%T"
#define SRT_DATETIME_FORMAT SRT_DATE_FORMAT " " SRT_TIME_FORMAT

namespace SRTPluginRE9::DateTime
{
	inline const std::chrono::system_clock::time_point GetUTCDateTime()
	{
		return std::chrono::system_clock::now();
	}

	inline const std::chrono::local_time<std::chrono::system_clock::duration> GetLocalDateTime()
	{
		return std::chrono::zoned_time{std::chrono::current_zone(), GetUTCDateTime()}.get_local_time();
	}
};

#endif
