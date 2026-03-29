#include "Thread.h"

namespace SRTPluginRE9::Thread
{
	const HRESULT SetThreadName(HANDLE hThread, const std::string &threadName)
	{
		int bufferSize = MultiByteToWideChar(CP_UTF8, 0, threadName.c_str(), -1, nullptr, 0);
		std::wstring wThreadName(bufferSize, L'\0');
		int bufferWritten = MultiByteToWideChar(CP_UTF8, 0, threadName.c_str(), -1, wThreadName.data(), bufferSize);
		if (bufferWritten != bufferSize)
			return E_FAIL;
		return SetThreadDescription(hThread, wThreadName.c_str());
	}
}
