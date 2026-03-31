#include "DllMain.h"

#include "Hook.h"
#include "Logger.h"
#include "Thread.h"
#include <DbgHelp.h>
#include <chrono>
#include <mutex>
#include <string>

HMODULE g_dllModule = nullptr;
HANDLE g_mainThread = nullptr;
FILE *g_logFile = nullptr;
SRTPluginRE9::Logger::Logger *logger = nullptr;
SRTPluginRE9::Logger::LogViewerData *g_LogViewerData = nullptr;
std::mutex g_LogMutex;

const std::wstring GetCrashDumpFileName()
{
	return std::format(L"SRTPluginRE9_{:%Y%m%d-%H%M%S}UTC.dmp", std::chrono::utc_clock::now());
}

LONG WINAPI SRTUnhandledExceptionFilter(EXCEPTION_POINTERS *pExceptionInfo)
{
	// Write error information to our log file.
	if (logger)
		logger->LogMessage("Exception {:x} occurred at {:p}!", pExceptionInfo->ExceptionRecord->ExceptionCode, pExceptionInfo->ExceptionRecord->ExceptionAddress);

	// Create the dump file
	auto hFile = CreateFileW(
	    GetCrashDumpFileName().c_str(),
	    GENERIC_WRITE,
	    0, // Exclusive access.
	    nullptr,
	    CREATE_ALWAYS,
	    FILE_ATTRIBUTE_NORMAL,
	    nullptr);

	if (hFile != INVALID_HANDLE_VALUE)
	{
		MINIDUMP_EXCEPTION_INFORMATION miniDumpEI{
		    .ThreadId = GetCurrentThreadId(),
		    .ExceptionPointers = pExceptionInfo,
		    .ClientPointers = FALSE};

		MiniDumpWriteDump(
		    GetCurrentProcess(),
		    GetCurrentProcessId(),
		    hFile,
		    MiniDumpNormal, // See below for richer options
		    &miniDumpEI,
		    nullptr, // UserStreamParam (optional)
		    nullptr  // CallbackParam   (optional)
		);

		CloseHandle(hFile);
	}

	// Let other handlers process the exception including REFramework or WER (Windows Error Reporting).
	return EXCEPTION_CONTINUE_SEARCH;
}

// DLL Entry Point
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID)
{
	HRESULT hResult;

	if (ul_reason_for_call == DLL_PROCESS_ATTACH)
	{
		g_dllModule = hModule;

		// Disable thread notifications for performance
		DisableThreadLibraryCalls(hModule);

		// Set unhandled exception handler.
		SetUnhandledExceptionFilter(SRTUnhandledExceptionFilter);

		if ((g_logFile = _fsopen("SRTPluginRE9.log", "w", SH_DENYNO)) != nullptr)
		{
			{
				std::lock_guard<std::mutex> lock(g_LogMutex);
				g_LogViewerData = new SRTPluginRE9::Logger::LogViewerData();
			}
			logger = new SRTPluginRE9::Logger::Logger(g_logFile, g_LogViewerData);

			logger->LogMessage("{} {}: v{}\n", SRTPluginRE9::GameNameShort, SRTPluginRE9::ToolNameShort, SRTPluginRE9::Version::SemVer);
			logger->LogMessage("DllMain() entered with reason: {:d}\n", ul_reason_for_call);

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
			logger->LogMessage("Detaching!\n");
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
