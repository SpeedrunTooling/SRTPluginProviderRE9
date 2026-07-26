#ifndef SRTPLUGINRE9_CRASHHANDLER_H
#define SRTPLUGINRE9_CRASHHANDLER_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <atomic>
#include <cstdint>
#include <windows.h>

namespace SRTPluginRE9::Hook::CrashHandler
{
	/// @brief Identity of a PE image, read from its IMAGE_DIRECTORY_ENTRY_DEBUG RSDS record.
	/// This is what a debugger matches a .pdb against — logging it means every bug report
	/// says exactly which symbols to fetch.
	struct BuildIdentity
	{
		GUID PdbGuid{};
		uint32_t PdbAge = 0;
		uint32_t LinkTimestamp = 0;
		uintptr_t ImageBase = 0;
		uint32_t SizeOfImage = 0;
		char PdbPath[MAX_PATH]{};
		bool Valid = false;
	};

	/// @brief A hook target, and whether the detour actually took.
	/// The owning module matters: another injector may have detoured the vtable first, in
	/// which case we are hooking *their* trampoline rather than the real API. SafetyHook
	/// reports failure by producing a falsy inline hook, which is easy to miss at runtime but
	/// invaluable in a crash report.
	struct HookRecord
	{
		char Name[32]{};
		uintptr_t Target = 0;
		bool Installed = false;
	};

	inline constexpr uint32_t MaxHookRecords = 12;

	/// @brief Snapshot of SRT state embedded in the dump and the text crash report.
	/// Every member is plain data so the exception filter can read it without taking a lock.
	struct CrashContext
	{
		// Set by Hook::MainLoop() once the game version has been detected.
		char GameVersionName[64]{};
		uintptr_t GameBaseAddress = 0;
		uintptr_t RankManager = 0;
		uintptr_t CharacterManager = 0;

		// Set as each hook is attached.
		HookRecord Hooks[MaxHookRecords]{};
		std::atomic<uint32_t> HookCount{0};

		// Heartbeat from the memory read loop — tells us whether SRT was still running.
		std::atomic<uint64_t> ReadLoopIterations{0};
	};

	inline CrashContext g_CrashContext{};

	/// @brief Reads a loaded module's RSDS debug record. Safe to call at any time.
	/// @param module The module to inspect, or nullptr for the main executable.
	/// @return The build identity; Valid is false if the module has no RSDS record.
	[[nodiscard]] BuildIdentity GetBuildIdentity(HMODULE module) noexcept;

	/// @brief Installs the unhandled exception filter. Call once from DLL_PROCESS_ATTACH.
	/// @param module The plugin's own module handle.
	void Install(HMODULE module) noexcept;

	/// @brief Logs the plugin's build identity. Call once the logger exists.
	void LogBuildIdentity() noexcept;

	/// @brief Records a hook target so the crash report can name its owning module.
	/// Call when the target address is resolved, before the detour is attempted.
	/// @param name Short label, e.g. "Present".
	/// @param target The address to be detoured.
	void RecordHook(const char *name, void *target) noexcept;

	/// @brief Updates a previously recorded hook with whether its detour succeeded.
	/// @param name The same label passed to RecordHook.
	/// @param installed Whether the resulting hook object is valid.
	void MarkHookInstalled(const char *name, bool installed) noexcept;

	/// @brief Reserves stack for the calling thread so a stack-overflow crash (0xC00000FD)
	/// still leaves room for the filter to run. Call on threads that execute our code.
	void ReserveStackForCrashHandling() noexcept;
}

#endif
