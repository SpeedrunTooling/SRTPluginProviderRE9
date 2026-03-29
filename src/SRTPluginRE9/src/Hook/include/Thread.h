#ifndef SRTPLUGINRE9_THREAD_H
#define SRTPLUGINRE9_THREAD_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <string>
#include <windows.h>

namespace SRTPluginRE9::Thread
{
	const HRESULT SetThreadName(HANDLE hThread, const std::string &threadName);
}

#endif