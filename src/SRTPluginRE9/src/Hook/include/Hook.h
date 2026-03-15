#ifndef SRTPLUGINRE9_HOOK_H
#define SRTPLUGINRE9_HOOK_H

#define DIRECTINPUT_VERSION 0x0800

#include "DescriptorHeapAllocator.h"
#include "GameObjects.h"
#include "Protected_Ptr.h"
#include "VTableScanner.h"
#include <MinHook.h>
#include <d3d12.h>
#include <dinput.h>
#include <dxgi.h>
#include <dxgi1_4.h>
#include <expected>
#include <format>
#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <optional>
#include <print>
#include <string>
#include <vector>
#include <windows.h>
#include <wrl/client.h>

namespace SRTPluginRE9::Hook
{
	[[nodiscard]] std::optional<std::uintptr_t> get_module_base(const wchar_t *module_name = nullptr) noexcept;

	/// @brief Attempts to hook the target function, calling the hook function in its place.
	/// @tparam FuncT The typedef prototype of the function to hook.
	/// @param target The target function.
	/// @param hook The hook function.
	/// @param original A reference to the original function.
	/// @param status The status code for the hook.
	/// @return A boolean indicating whether we succeeded in creating and enabling the hook.
	template <typename FuncT>
	bool TryHookFunction(FuncT target, FuncT hook, FuncT *original, MH_STATUS &status)
	{
		status = MH_CreateHook(static_cast<LPVOID>(target), static_cast<LPVOID>(hook), static_cast<LPVOID *>(original));
		if (status != MH_OK)
			return false;

		status = MH_EnableHook(static_cast<LPVOID>(target));
		if (status != MH_OK)
			return false;

		return true;
	}

	// Original function pointers (set by MinHook)
	using PFN_Present = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain3 *, UINT, UINT);
	using PFN_ResizeBuffers = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain *, UINT, UINT, UINT, DXGI_FORMAT, UINT);
	using PFN_ExecuteCommandLists = void(STDMETHODCALLTYPE *)(ID3D12CommandQueue *, UINT, ID3D12CommandList *const *);
	using PFN_GetDeviceState = HRESULT(STDMETHODCALLTYPE *)(IDirectInputDevice8W *, DWORD, LPVOID);
	using PFN_GetDeviceData = HRESULT(STDMETHODCALLTYPE *)(IDirectInputDevice8W *, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);

	inline PFN_Present oPresent = nullptr;
	inline PFN_ResizeBuffers oResizeBuffers = nullptr;
	inline PFN_ExecuteCommandLists oExecuteCommandLists = nullptr;
	inline PFN_GetDeviceState oGetDeviceState = nullptr;
	inline PFN_GetDeviceData oGetDeviceData = nullptr;

	class Hook
	{
		Hook() {}
		bool Startup();

		[[nodiscard]] auto SetVTables() -> std::expected<VTableAddresses, std::string>;

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
	};
}

#endif
