#include "DllMain.h"

#include "Hook.h"
#include "Logger.h"
#include <mutex>

HMODULE g_dllModule = nullptr;
HANDLE g_mainThread = nullptr;
FILE *g_logFile = nullptr;
SRTPluginRE9::Logger::Logger *logger = nullptr;
SRTPluginRE9::Logger::LoggerUIData *g_LoggerUIData = nullptr;
std::mutex g_LogMutex;

// DLL Entry Point
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID)
{
	if (ul_reason_for_call == DLL_PROCESS_ATTACH)
	{
		g_dllModule = hModule;

		// Disable thread notifications for performance
		DisableThreadLibraryCalls(hModule);

		if ((g_logFile = _fsopen("SRTPluginRE9.log", "w", SH_DENYNO)) != nullptr)
		{
			{
				std::lock_guard<std::mutex> lock(g_LogMutex);
				g_LoggerUIData = new SRTPluginRE9::Logger::LoggerUIData();
			}
			logger = new SRTPluginRE9::Logger::Logger(g_logFile, g_LoggerUIData);
			logger->LogMessage("DllMain() entered with reason: {:d}\n", ul_reason_for_call);

			// Create thread to avoid blocking loader lock
			g_mainThread = CreateThread(
			    nullptr,
			    0,
			    SRTPluginRE9::Hook::Hook::ThreadMain,
			    nullptr,
			    0,
			    nullptr);

			if (g_mainThread)
			{
				CloseHandle(g_mainThread);
			}
		}
	}
	else if (ul_reason_for_call == DLL_PROCESS_DETACH)
	{
		if (g_logFile != nullptr)
		{
			logger->LogMessage("Detaching!\n");
			{
				std::lock_guard<std::mutex> lock(g_LogMutex);
				delete g_LoggerUIData;
				g_LoggerUIData = nullptr;
			}
			delete logger;
			logger = nullptr;
			std::fclose(g_logFile);
			g_logFile = nullptr;
		}
	}

	return TRUE;
}
