#include "Logger.h"
#include "DateTime.h"

#include <cstdio>

namespace SRTPluginRE9::Logger
{
	void Logger::SetUIPtr(SRTPluginRE9::Hook::UI *pSRTUI)
	{
		this->srtUI = pSRTUI;
	}

	void Logger::LogMessage(const std::string_view message)
	{
		const std::string format = std::format("[{:" SRT_DATETIME_FORMAT "}] {}", SRTPluginRE9::DateTime::GetUTCDateTime(), message);
		if (out)
		{
			std::fwrite(format.data(), 1, format.size(), out);
			std::fflush(out);
		}
		LogMessageUI("%s", format.data());
	}

	void Logger::LogException(const std::exception &ex, const std::source_location &location)
	{
		const std::string format = std::format("[{:" SRT_DATETIME_FORMAT "}] {:s} Exception: {:s}\n{:s} @ {:s}:{:d}:{:d}\n",
		                                       SRTPluginRE9::DateTime::GetUTCDateTime(),
		                                       typeid(ex).name(),
		                                       ex.what(),
		                                       location.function_name(),
		                                       location.file_name(),
		                                       location.line(),
		                                       location.column());
		if (out)
		{
			std::fwrite(format.data(), 1, format.size(), out);
			std::fflush(out);
		}
		LogMessageUI("%s", format.data());
	}

	void Logger::LogMessageUI(const char *fmt, ...) IM_FMTARGS(2)
	{
		if (logViewerData)
		{
			int old_size = logViewerData->Buffer.size();
			va_list args;
			va_start(args, fmt);
			logViewerData->Buffer.appendfv(fmt, args);
			va_end(args);
			for (const int new_size = logViewerData->Buffer.size(); old_size < new_size; old_size++)
				if (logViewerData->Buffer[old_size] == '\n')
					logViewerData->LineOffsets.push_back(old_size + 1);
		}
	}
}
