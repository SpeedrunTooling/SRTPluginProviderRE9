#include "DInput8Hook.h"
#include "Globals.h"
#include <safetyhook.hpp>

namespace SRTPluginRE9::DInput8Hook
{
	static HRESULT CreateDInput8(IDirectInput8W **ppDInput8W)
	{
		HRESULT hr;

		__try
		{
			hr = DirectInput8Create(
			    GetModuleHandleW(nullptr),
			    DIRECTINPUT_VERSION,
			    IID_IDirectInput8W,
			    reinterpret_cast<void **>(ppDInput8W),
			    nullptr);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			hr = E_FAIL;
		}

		return hr;
	}

	static HRESULT CreateDInput8Device(IDirectInput8W *pDInput8W, IDirectInputDevice8W **ppDInputDevice8W)
	{
		HRESULT hr;

		__try
		{
			hr = pDInput8W->CreateDevice(
			    GUID_SysKeyboard,
			    ppDInputDevice8W,
			    nullptr);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			hr = E_FAIL;
		}

		return hr;
	}

	DInput8Hook::DInput8Hook() // Resolve and store DInput8 VTables.
	{
		HRESULT hr;

		Microsoft::WRL::ComPtr<IDirectInput8W> directInput;
		if (FAILED(hr = CreateDInput8(directInput.GetAddressOf())))
		{
			logger->LogMessage("DInput8Hook::DInput8Hook(): DirectInput8Create failed: {:#x}", static_cast<uint32_t>(hr));
			return;
		}

		Microsoft::WRL::ComPtr<IDirectInputDevice8W> tmpInputDevice;
		if (FAILED(hr = CreateDInput8Device(directInput.Get(), tmpInputDevice.GetAddressOf())))
		{
			logger->LogMessage("DInput8Hook::DInput8Hook(): CreateDevice failed: {:#x}", static_cast<uint32_t>(hr));
			return;
		}

		auto vtable = *reinterpret_cast<void ***>(tmpInputDevice.Get());
		vtableAddresses.getDeviceState = vtable[9];
		vtableAddresses.getDeviceData = vtable[10];

		tmpInputDevice.Reset();
		directInput.Reset();

		logger->LogMessage(
		    "DInput8Hook::DInput8Hook(): Addresses:\n"
		    "  GetDeviceState={:p}\n"
		    "  GetDeviceData={:p}\n",
		    vtableAddresses.getDeviceState, vtableAddresses.getDeviceData);
	}

	DInput8Hook &DInput8Hook::GetInstance() // Return the singleton instance of this class.
	{
		static DInput8Hook instance;
		return instance;
	}

	bool DInput8Hook::AttachHooks(PFN_GetDeviceState hkGetDeviceState, PFN_GetDeviceData hkGetDeviceData)
	{
		oGetDeviceState = safetyhook::create_inline(vtableAddresses.getDeviceState, reinterpret_cast<void *>(hkGetDeviceState));
		oGetDeviceData = safetyhook::create_inline(vtableAddresses.getDeviceData, reinterpret_cast<void *>(hkGetDeviceData));

		logger->LogMessage("DInput8Hook::AttachHooks() - All DInput8 hooks attached.\n");
		return true;
	}

	void DInput8Hook::DetachHooks()
	{
		oGetDeviceData.reset();
		oGetDeviceState.reset();

		logger->LogMessage("DInput8Hook::DetachHooks() - All DInput8 hooks detached.\n");
	}

	DInput8Hook::PFN_GetDeviceState DInput8Hook::GetOriginalGetDeviceState() const { return oGetDeviceState.original<PFN_GetDeviceState>(); }
	DInput8Hook::PFN_GetDeviceData DInput8Hook::GetOriginalGetDeviceData() const { return oGetDeviceData.original<PFN_GetDeviceData>(); }
}
