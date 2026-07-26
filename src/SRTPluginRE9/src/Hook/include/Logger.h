#ifndef SRTPLUGINRE9_LOGGER_H
#define SRTPLUGINRE9_LOGGER_H

#include "UI.h"
#include "imgui.h"
#include <atomic>
#include <cstddef>
#include <exception>
#include <format>
#include <source_location>

namespace SRTPluginRE9::Logger
{
	// Lock-free tail of the log. The crash handler reads this without taking g_LogMutex —
	// a crash can happen while the mutex is held, so locking there would deadlock. A torn
	// read is acceptable in exchange for never blocking.
	inline constexpr size_t LogRingCapacity = 64 * 1024;
	extern char g_LogRing[LogRingCapacity];
	extern std::atomic<size_t> g_LogRingWritten; // Total bytes ever appended, not the ring offset.

	/// @brief Appends to the lock-free ring. Safe from any thread.
	void AppendToLogRing(const char *data, size_t size) noexcept;

	/// @brief Copies the ring's contents into dest in chronological order.
	/// @return Number of bytes written to dest.
	size_t CopyLogRing(char *dest, size_t destSize) noexcept;

	struct LogViewerData
	{
		ImGuiTextBuffer Buffer;
		ImVector<int> LineOffsets;
		LogViewerData() { LineOffsets.push_back(0); }
	};

	class Logger
	{
		FILE *out;
		LogViewerData *logViewerData;
		SRTPluginRE9::Hook::UI *srtUI;

	public:
		explicit Logger(FILE *out, LogViewerData *logViewerData) : out(out), logViewerData(logViewerData) {}
		~Logger()
		{
			out = nullptr;
			logViewerData = nullptr;
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
