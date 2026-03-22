#include "VTableScanner.h"
#include "Hook.h"

#include <d3d12.h>
#include <dinput.h>
#include <dxgi.h>
#include <dxgi1_4.h>
#include <format>
#include <wrl/client.h>

namespace SRTPluginRE9::Hook
{
	auto VTableResolver::Resolve()
	    -> std::expected<VTableAddresses, std::string>
	{
		auto dx12VTables = VTableResolver::ResolveDX12VTables();
		if (!dx12VTables)
			return std::unexpected(dx12VTables.error());

		auto dInput8VTables = VTableResolver::ResolveDInput8VTables();
		if (!dInput8VTables)
			return std::unexpected(dInput8VTables.error());

		return VTableAddresses{*dx12VTables, *dInput8VTables};
	}

	auto VTableResolver::ResolveDX12VTables()
	    -> std::expected<DX12VTableAddresses, std::string>
	{
		DX12VTableAddresses returnValue{};
		HRESULT hr;

		Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
		hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
		if (FAILED(hr))
			return std::unexpected(std::format("CreateDXGIFactory1 failed: {:#x}", static_cast<uint32_t>(hr)));

		UINT AdapterIndex = 0; 
		Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
		Microsoft::WRL::ComPtr<ID3D12Device> tmpDevice;
		for (; DXGI_ERROR_NOT_FOUND != factory->EnumAdapters1(AdapterIndex, &adapter); ++AdapterIndex) {
			DXGI_ADAPTER_DESC1 Desc;
			if (adapter->GetDesc1(&Desc) < 0) {
				continue;
			}

			if (Desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
				continue;
			}

			hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&tmpDevice));
			if (hr >= 0) {
				break;
			}
		}

		if (FAILED(hr))
			return std::unexpected(std::format("D3D12CreateDevice failed: {:#x}", static_cast<uint32_t>(hr)));

		constexpr D3D12_COMMAND_QUEUE_DESC queueDesc{
		    .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
		    .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
		};
		Microsoft::WRL::ComPtr<ID3D12CommandQueue> tmpQueue;
		hr = tmpDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&tmpQueue));
		if (FAILED(hr))
			return std::unexpected(std::format("CreateCommandQueue failed: {:#x}", static_cast<uint32_t>(hr)));

		// Read ExecuteCommandLists from the hardware queue vtable
		auto queueVTable = *reinterpret_cast<void ***>(tmpQueue.Get());
		returnValue.executeCommandLists = queueVTable[10];

		DXGI_SWAP_CHAIN_DESC1 swapDesc{
		    .Width = 1,
		    .Height = 1,
		    .Format = DXGI_FORMAT_B8G8R8A8_UNORM,
		    .SampleDesc = {.Count = 1, .Quality = 0},
		    .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
		    .BufferCount = 2,
		    .SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL,
		    .AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED,
		};

		Microsoft::WRL::ComPtr<IDXGISwapChain1> tmpSwapChain1;
		hr = factory->CreateSwapChainForComposition(
		    tmpQueue.Get(), &swapDesc, nullptr, &tmpSwapChain1);

		if (FAILED(hr))
		{
			logger->LogMessage(
			    "VTableResolver - CreateSwapChainForComposition failed: {:#x}, "
			    "falling back to CreateSwapChainForHwnd with message-only window\n",
			    static_cast<uint32_t>(hr));

			// Fallback: use CreateSwapChainForHwnd with a message-only window.
			// This may trigger other tools that hook, but it's better than failing entirely.
			WNDCLASSEXW wndClass{
			    .cbSize = sizeof(wndClass),
			    .lpfnWndProc = DefWindowProcW,
			    .lpszClassName = L"SRTPluginDummy"};
			RegisterClassExW(&wndClass);
			const auto hwnd = CreateWindowExW(0, wndClass.lpszClassName, L"", WS_OVERLAPPEDWINDOW,
			                                  0, 0, 100, 100, nullptr, nullptr,
			                                  wndClass.hInstance, nullptr);

			swapDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
			swapDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
			swapDesc.Width = 2;
			swapDesc.Height = 2;

			hr = factory->CreateSwapChainForHwnd(
			    tmpQueue.Get(), hwnd, &swapDesc, nullptr, nullptr, &tmpSwapChain1);

			DestroyWindow(hwnd);
			UnregisterClassW(wndClass.lpszClassName, wndClass.hInstance);

			if (FAILED(hr))
				return std::unexpected(std::format("CreateSwapChainForHwnd fallback failed: {:#x}",
				                                   static_cast<uint32_t>(hr)));
		}

		Microsoft::WRL::ComPtr<IDXGISwapChain3> tmpSwapChain3;
		hr = tmpSwapChain1.As(&tmpSwapChain3);
		if (FAILED(hr) || !tmpSwapChain3)
			return std::unexpected(std::format("QI IDXGISwapChain3 failed: {:#x}", static_cast<uint32_t>(hr)));

		const auto swapVTable = *reinterpret_cast<void ***>(tmpSwapChain3.Get());
		returnValue.present = swapVTable[8];
		returnValue.resizeBuffers = swapVTable[13];

		tmpSwapChain3.Reset();
		tmpSwapChain1.Reset();
		tmpQueue.Reset();
		tmpDevice.Reset();
		adapter.Reset();
		factory.Reset();

		logger->LogMessage(
		    "ResolveDX12VTables() - Addresses:\n"
		    "  Present={:p}\n"
		    "  ResizeBuffers={:p}\n"
		    "  ExecuteCommandLists={:p}\n",
		    returnValue.present, returnValue.resizeBuffers, returnValue.executeCommandLists);

		return returnValue;
	}

	HRESULT CreateDInput8(IDirectInput8W **ppDInput8W)
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
			hr = S_FALSE;
		}

		return hr;
	}

	HRESULT CreateDInput8Device(IDirectInput8W *pDInput8W, IDirectInputDevice8W **ppDInputDevice8W)
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
			hr = S_FALSE;
		}

		return hr;
	}

	auto VTableResolver::ResolveDInput8VTables()
	    -> std::expected<DInput8VTableAddresses, std::string>
	{
		DInput8VTableAddresses returnValue{};
		HRESULT hr;

		Microsoft::WRL::ComPtr<IDirectInput8W> directInput;
		hr = CreateDInput8(directInput.GetAddressOf());
		if (FAILED(hr))
			return std::unexpected(std::format("DirectInputHook - DirectInput8Create failed: {:#x}\n",
			                                   static_cast<uint32_t>(hr)));

		Microsoft::WRL::ComPtr<IDirectInputDevice8W> tmpInputDevice;
		hr = CreateDInput8Device(directInput.Get(), tmpInputDevice.GetAddressOf());
		if (FAILED(hr))
			return std::unexpected(std::format("DirectInputHook - CreateDevice failed: {:#x}\n",
			                                   static_cast<uint32_t>(hr)));

		auto vtable = *reinterpret_cast<void ***>(tmpInputDevice.Get());
		returnValue.getDeviceState = vtable[9];
		returnValue.getDeviceData = vtable[10];

		tmpInputDevice.Reset();
		directInput.Reset();

		logger->LogMessage(
		    "ResolveDInput8VTables() - Addresses:\n"
		    "  GetDeviceState={:p}\n"
		    "  GetDeviceData={:p}\n",
		    returnValue.getDeviceState, returnValue.getDeviceData);

		return returnValue;
	}
}
