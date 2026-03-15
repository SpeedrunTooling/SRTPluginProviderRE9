#ifndef SRTPLUGINRE9_LOGGER_H
#define SRTPLUGINRE9_LOGGER_H

#include "UI.h"
#include "imgui.h"
#include <exception>
#include <format>
#include <source_location>

namespace SRTPluginRE9::Logger
{
	struct LoggerUIData
	{
		ImGuiTextBuffer Buffer;
		ImVector<int> LineOffsets;
	};

	class Logger
	{
		FILE *out;
		LoggerUIData *loggerUIData;
		SRTPluginRE9::Hook::UI *srtUI;

	public:
		explicit Logger(FILE *out, LoggerUIData *loggerUIData) : out(out), loggerUIData(loggerUIData) {}
		~Logger()
		{
			out = nullptr;
			loggerUIData = nullptr;
			srtUI = nullptr;
		}

		void SetUIPtr(SRTPluginRE9::Hook::UI *pSRTUI);

		void LogMessage(std::string_view message);
		template <typename... Args>
		void LogMessage(const std::string_view fmt, Args &&...args)
		{
			LogMessage(std::vformat(fmt, std::make_format_args(args...)));
		}
		void LogException(const std::exception &ex, const std::source_location &location = std::source_location::current());

	private:
		void LogMessageUI(const char *fmt, ...) IM_FMTARGS(2);
	};
}

#endif
