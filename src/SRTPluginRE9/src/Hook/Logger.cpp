#include "Logger.h"

#include <cstdio>

namespace SRTPluginRE9::Logger
{
	void Logger::SetUIPtr(SRTPluginRE9::Hook::UI *pSRTUI)
	{
		this->srtUI = pSRTUI;
	}

	void Logger::LogMessage(const std::string_view message)
	{
		if (out)
		{
			std::fwrite(message.data(), 1, message.size(), out);
			std::fflush(out);
		}
		LogMessageUI("%s", message.data());
	}

	void Logger::LogException(const std::exception &ex, const std::source_location &location)
	{
		const std::string format = std::format("[RE2R-R] {:s} Exception: {:s}\n{:s} @ {:s}:{:d}:{:d}\n",
		                                       typeid(ex).name(),
		                                       ex.what(),
		                                       location.function_name(),
		                                       location.file_name(),
		                                       location.line(),
		                                       location.column());
		if (out)
		{
			fputs(format.c_str(), out);
			fflush(out);
		}
		LogMessageUI("%s", format.c_str());
	}

	void Logger::LogMessageUI(const char *fmt, ...) IM_FMTARGS(2)
	{
		if (loggerUIData)
		{
			int old_size = loggerUIData->Buffer.size();
			va_list args;
			va_start(args, fmt);
			loggerUIData->Buffer.appendfv(fmt, args);
			va_end(args);
			for (const int new_size = loggerUIData->Buffer.size(); old_size < new_size; old_size++)
				if (loggerUIData->Buffer[old_size] == '\n')
					loggerUIData->LineOffsets.push_back(old_size + 1);
		}
	}
}
