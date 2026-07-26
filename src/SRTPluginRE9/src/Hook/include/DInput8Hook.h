#ifndef SRTPLUGINRE9_DINPUT8HOOK_H
#define SRTPLUGINRE9_DINPUT8HOOK_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#define DIRECTINPUT_VERSION 0x0800

#include <dinput.h>
#include <safetyhook.hpp>
#include <windows.h>
#include <wrl/client.h>

namespace SRTPluginRE9::DInput8Hook
{
	/// @brief A hook class wrapping DirectInput8 resources.
	class DInput8Hook
	{
	public:
		// Function pointer definitions.
		using PFN_GetDeviceState = HRESULT(STDMETHODCALLTYPE *)(IDirectInputDevice8W *, DWORD, LPVOID);
		using PFN_GetDeviceData = HRESULT(STDMETHODCALLTYPE *)(IDirectInputDevice8W *, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);

	private:
		DInput8Hook(); // Resolve and store DInput8 VTables.

		// Original function pointers (set by SafetyHook)
		SafetyHookInline oGetDeviceState{};
		SafetyHookInline oGetDeviceData{};

		struct DInput8VTableAddresses
		{
			void *getDeviceState = nullptr;
			void *getDeviceData = nullptr;
		};

		DInput8VTableAddresses vtableAddresses{};

	public:
		// Deleted methods (only allow default ctor/dtor, move ctor, and move assignment)
		// DInput8Hook(void) = delete;                              // default ctor (1)
		// ~DInput8Hook(void) = delete;                             // default dtor (2)
		DInput8Hook(const DInput8Hook &) = delete; // copy ctor (3)
		// DInput8Hook(const DInput8Hook &&) = delete;              // move ctor (4)
		DInput8Hook &operator=(const DInput8Hook &) = delete; // copy assignment (5)
		                                                      // DInput8Hook &operator=(const DInput8Hook &&) = delete; // move assignment (6)

		/// @brief Get the singleton instance of this class.
		/// @return The singleton instance of this class.
		static DInput8Hook &GetInstance();

		/// @brief Attach MinHook detours for GetDeviceState and GetDeviceData.
		/// @param hkGetDeviceState The hook function for GetDeviceState.
		/// @param hkGetDeviceData The hook function for GetDeviceData.
		/// @return True if all hooks were created successfully.
		bool AttachHooks(PFN_GetDeviceState hkGetDeviceState, PFN_GetDeviceData hkGetDeviceData);

		/// @brief Detach MinHook detours for GetDeviceState and GetDeviceData.
		void DetachHooks();

		/// @brief Get the original GetDeviceState function pointer.
		PFN_GetDeviceState GetOriginalGetDeviceState() const;

		/// @brief Get the original GetDeviceData function pointer.
		PFN_GetDeviceData GetOriginalGetDeviceData() const;
	};
}

#endif
