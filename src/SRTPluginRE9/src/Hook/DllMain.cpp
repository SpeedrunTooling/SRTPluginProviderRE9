#include "DllMain.h"

#include "CrashHandler.h"
#include "Hook.h"
#include "Logger.h"
#include "Thread.h"
#include <mutex>

HMODULE g_dllModule = nullptr;
HANDLE g_mainThread = nullptr;
FILE *g_logFile = nullptr;
SRTPluginRE9::Logger::Logger *logger = nullptr;
SRTPluginRE9::Logger::LogViewerData *g_LogViewerData = nullptr;
std::mutex g_LogMutex;

// DLL Entry Point
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID)
{
	HRESULT hResult;

	if (ul_reason_for_call == DLL_PROCESS_ATTACH)
	{
		g_dllModule = hModule;

		// Disable thread notifications for performance
		DisableThreadLibraryCalls(hModule);

		// Install the crash handler before anything else can fault.
		SRTPluginRE9::Hook::CrashHandler::Install(hModule);

		if ((g_logFile = _fsopen("SRTPluginRE9.log", "w", SH_DENYNO)) != nullptr)
		{
			{
				std::lock_guard<std::mutex> lock(g_LogMutex);
				g_LogViewerData = new SRTPluginRE9::Logger::LogViewerData();
			}
			logger = new SRTPluginRE9::Logger::Logger(g_logFile, g_LogViewerData);

			logger->LogMessage("{} {}: v{}\n", SRTPluginRE9::GameNameShort, SRTPluginRE9::ToolNameShort, SRTPluginRE9::Version::SemVer);

			// Self-identify the exact symbols this binary needs, so a crash report never
			// leaves anyone guessing which .pdb to fetch.
			SRTPluginRE9::Hook::CrashHandler::LogBuildIdentity();

			logger->LogMessage("Press {} to show UI. Press {} to shutdown.\n", "F7", "F8");
			logger->LogMessage("{:-<50}\n", "");
			logger->LogMessage("DllMain() entered with reason: {}\n", __DEFINE_TO_STRING(DLL_PROCESS_ATTACH));

			hResult = SRTPluginRE9::Thread::SetThreadName(GetCurrentThread(), std::format("{} {} Entry Thread", SRTPluginRE9::GameNameShort, SRTPluginRE9::ToolNameShort));
			if (FAILED(hResult))
				logger->LogMessage("DllMain() failed to set thread description: {:d}\n", static_cast<uint32_t>(hResult));

			// Create thread to avoid blocking loader lock
			g_mainThread = CreateThread(
			    nullptr,
			    0,
			    SRTPluginRE9::Hook::Hook::ThreadMain,
			    nullptr,
			    0,
			    nullptr);

			if (g_mainThread)
				CloseHandle(g_mainThread);
		}
	}
	else if (ul_reason_for_call == DLL_PROCESS_DETACH)
	{
		if (g_logFile != nullptr)
		{
			logger->LogMessage("DllMain() entered with reason: {}\n", __DEFINE_TO_STRING(DLL_PROCESS_DETACH));
			{
				std::lock_guard<std::mutex> lock(g_LogMutex);
				delete g_LogViewerData;
				g_LogViewerData = nullptr;
			}
			delete logger;
			logger = nullptr;
			std::fclose(g_logFile);
			g_logFile = nullptr;
		}
	}

	return TRUE;
}
