#ifndef SRTPLUGINRE9_HOOK_H
#define SRTPLUGINRE9_HOOK_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "DInput8Hook.h"
#include "DX12Hook.h"
#include "GameObjects.h"
#include "Protected_Ptr.h"
#include <expected>
#include <format>
#include <imgui.h>
#include <optional>
#include <print>
#include <string>
#include <vector>
#include <windows.h>

namespace SRTPluginRE9::Hook
{
	[[nodiscard]] std::optional<std::uintptr_t> get_module_base(const wchar_t *module_name = nullptr) noexcept;

	class Hook
	{
		Hook() {}
		bool Startup();

	public:
		// Deleted methods
		// Hook(void) = delete;                        // default ctor (1)
		//~Hook(void) = delete;                       // default dtor (2)
		Hook(const Hook &) = delete; // copy ctor (3)
		// Hook(const Hook &&) = delete;            // move ctor (4)
		Hook &operator=(const Hook &) = delete; // copy assignment (5)
		// Hook &operator=(const Hook &&) = delete; // move assignment (6)
		void Shutdown();
		static Hook &GetInstance();
		static DWORD WINAPI ThreadMain(LPVOID lpThreadParameter);

		static void WINAPI MainLoop();
	};
}

#endif
