#include "Logger.h"
#include "DateTime.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace SRTPluginRE9::Logger
{
	char g_LogRing[LogRingCapacity]{};
	std::atomic<size_t> g_LogRingWritten{0};

	void AppendToLogRing(const char *data, size_t size) noexcept
	{
		if (data == nullptr || size == 0)
			return;

		// Only the newest LogRingCapacity bytes can survive, so drop anything older up front.
		if (size > LogRingCapacity)
		{
			data += size - LogRingCapacity;
			size = LogRingCapacity;
		}

		const size_t written = g_LogRingWritten.load(std::memory_order_relaxed);
		const size_t offset = written % LogRingCapacity;
		const size_t firstChunk = std::min(size, LogRingCapacity - offset);

		std::memcpy(g_LogRing + offset, data, firstChunk);
		if (size > firstChunk)
			std::memcpy(g_LogRing, data + firstChunk, size - firstChunk);

		g_LogRingWritten.store(written + size, std::memory_order_release);
	}

	size_t CopyLogRing(char *dest, size_t destSize) noexcept
	{
		if (dest == nullptr || destSize == 0)
			return 0;

		const size_t written = g_LogRingWritten.load(std::memory_order_acquire);
		const size_t available = std::min(written, LogRingCapacity);
		const size_t count = std::min(available, destSize);
		if (count == 0)
			return 0;

		// The oldest surviving byte sits `available` bytes behind the write cursor.
		const size_t start = (written - available) % LogRingCapacity;
		const size_t firstChunk = std::min(count, LogRingCapacity - start);

		std::memcpy(dest, g_LogRing + start, firstChunk);
		if (count > firstChunk)
			std::memcpy(dest + firstChunk, g_LogRing, count - firstChunk);

		return count;
	}

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
		AppendToLogRing(format.data(), format.size());
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
		AppendToLogRing(format.data(), format.size());
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
