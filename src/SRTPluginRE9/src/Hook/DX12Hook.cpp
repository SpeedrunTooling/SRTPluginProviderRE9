#include "DX12Hook.h"
#include "Globals.h"
#include "Logo.h"
#include "Render.h"
#include <MinHook.h>

namespace SRTPluginRE9::DX12Hook
{
	DX12Hook::DX12Hook() // Resolve and store DX12 VTables.
	{
		HRESULT hr;

		Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
		hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
		if (FAILED(hr))
		{
			logger->LogMessage("DX12Hook::DX12Hook(): CreateDXGIFactory1 failed: {:#x}", static_cast<uint32_t>(hr));
			return;
		}

		Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
		hr = factory->EnumAdapters1(0, &adapter);
		if (FAILED(hr))
		{
			logger->LogMessage("DX12Hook::DX12Hook(): EnumAdapters1 failed: {:#x}", static_cast<uint32_t>(hr));
			return;
		}

		Microsoft::WRL::ComPtr<ID3D12Device> tmpDevice;
		hr = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&tmpDevice));
		if (FAILED(hr))
		{
			logger->LogMessage("DX12Hook::DX12Hook(): D3D12CreateDevice failed: {:#x}", static_cast<uint32_t>(hr));
			return;
		}

		constexpr D3D12_COMMAND_QUEUE_DESC queueDesc{
		    .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
		    .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
		};
		Microsoft::WRL::ComPtr<ID3D12CommandQueue> tmpQueue;
		hr = tmpDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&tmpQueue));
		if (FAILED(hr))
		{
			logger->LogMessage("DX12Hook::DX12Hook(): CreateCommandQueue failed: {:#x}", static_cast<uint32_t>(hr));
			return;
		}

		// Read ExecuteCommandLists from the hardware queue vtable
		auto queueVTable = *reinterpret_cast<void ***>(tmpQueue.Get());
		vtableAddresses.executeCommandLists = queueVTable[10];

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
			    "DX12Hook::DX12Hook(): CreateSwapChainForComposition failed: {:#x}, "
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
			{
				logger->LogMessage("DX12Hook::DX12Hook(): CreateSwapChainForHwnd fallback failed: {:#x}",
				                   static_cast<uint32_t>(hr));
				return;
			}
		}

		Microsoft::WRL::ComPtr<IDXGISwapChain3> tmpSwapChain3;
		hr = tmpSwapChain1.As(&tmpSwapChain3);
		if (FAILED(hr) || !tmpSwapChain3)
		{
			logger->LogMessage("DX12Hook::DX12Hook(): QI IDXGISwapChain3 failed: {:#x}", static_cast<uint32_t>(hr));
			return;
		}

		const auto swapVTable = *reinterpret_cast<void ***>(tmpSwapChain3.Get());
		vtableAddresses.present = swapVTable[8];
		vtableAddresses.resizeBuffers = swapVTable[13];

		tmpSwapChain3.Reset();
		tmpSwapChain1.Reset();
		tmpQueue.Reset();
		tmpDevice.Reset();
		adapter.Reset();
		factory.Reset();

		logger->LogMessage(
		    "DX12Hook::DX12Hook(): Addresses:\n"
		    "  Present={:p}\n"
		    "  ResizeBuffers={:p}\n"
		    "  ExecuteCommandLists={:p}\n",
		    vtableAddresses.present, vtableAddresses.resizeBuffers, vtableAddresses.executeCommandLists);
	}

	DX12Hook &DX12Hook::GetInstance() // Return the singleton instance of this class.
	{
		static DX12Hook instance;
		return instance;
	}

	const HookState &DX12Hook::GetHookState()
	{
		return hookState;
	}

	HRESULT STDMETHODCALLTYPE DX12Hook::Initialize(IDXGISwapChain3 *pSwapChain, LONG_PTR hkWndProc) // Attach hooks and create resources.
	{
		HRESULT hResult = S_OK;

		if (FAILED(hResult = pSwapChain->GetDevice(IID_PPV_ARGS(&hookState.device))))
		{
			logger->LogMessage("DX12Hook::Initialize() - GetDevice failed: {:#x}\n", static_cast<uint32_t>(hResult));
			return hResult;
		}

		logger->LogMessage("DX12Hook::Initialize() - Using command queue: {:p}\n",
		                   reinterpret_cast<void *>(hookState.commandQueue));

		DXGI_SWAP_CHAIN_DESC desc{};
		pSwapChain->GetDesc(&desc);
		hookState.bufferCount = desc.BufferCount;
		hookState.gameWindow = desc.OutputWindow;
		hookState.frameContexts.resize(hookState.bufferCount);

		auto backBufferFormat = desc.BufferDesc.Format;
		logger->LogMessage("DX12Hook::Initialize() - BufferCount={}, Format={}, Window={:p}\n",
		                   hookState.bufferCount,
		                   static_cast<uint32_t>(backBufferFormat),
		                   reinterpret_cast<void *>(hookState.gameWindow));

		UINT rtvCapacity = hookState.bufferCount;
		UINT srvCapacity = 64;
		logger->LogMessage("DX12Hook::Initialize() - Allocating RTV ({}) and CBV, SRV, UAV ({}) Heaps\n", rtvCapacity, srvCapacity);

		auto heapResult = hookState.heaps.Init(hookState.device,
		                                       rtvCapacity,
		                                       srvCapacity);
		if (!heapResult)
		{
			logger->LogMessage("DX12Hook::Initialize() - Heap init failed: {}\n", heapResult.error());

			return E_FAIL;
		}

		for (UINT i = 0; i < hookState.bufferCount; ++i)
		{
			auto &frameContext = hookState.frameContexts[i];
			auto rtvHandle = hookState.heaps.rtv.Allocate();
			frameContext.rtvHandle = rtvHandle.cpu;

			if (FAILED(hResult = pSwapChain->GetBuffer(i, IID_PPV_ARGS(&frameContext.renderTarget))))
			{
				logger->LogMessage("DX12Hook::Initialize() - GetBuffer failed: {:#x}\n", static_cast<uint32_t>(hResult));

				return hResult;
			}

			D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{
			    .Format = backBufferFormat,
			    .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D,
			};

			hookState.device->CreateRenderTargetView(frameContext.renderTarget.Get(), &rtvDesc, rtvHandle.cpu);
			if (FAILED(hResult = hookState.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frameContext.commandAllocator))))
			{
				logger->LogMessage("DX12Hook::Initialize() - CreateCommandAllocator failed: {:#x}\n", static_cast<uint32_t>(hResult));

				return hResult;
			}
		}

		if (FAILED(hResult = hookState.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, hookState.frameContexts[0].commandAllocator.Get(), nullptr, IID_PPV_ARGS(&hookState.commandList))))
		{
			logger->LogMessage("DX12Hook::Initialize() - CreateCommandList failed: {:#x}\n", static_cast<uint32_t>(hResult));

			return hResult;
		}
		hookState.commandList->Close();

		if (FAILED(hResult = hookState.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&hookState.fence))))
		{
			logger->LogMessage("DX12Hook::Initialize() - CreateFence failed: {:#x}\n", static_cast<uint32_t>(hResult));

			return hResult;
		}
		hookState.fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);

		// Load logo texture
		logger->LogMessage("DX12Hook::Initialize() - Allocating on SRV heap for logo.\n");
		hookState.logoHandle = hookState.heaps.srv.Allocate();
		hookState.logoWidth = g_srtLogo_width;
		hookState.logoHeight = g_srtLogo_height;
		logger->LogMessage("DX12Hook::Initialize() - Allocated handle {:p} on SRV heap.\n", reinterpret_cast<void *>(hookState.logoHandle.cpu.ptr));

		auto logoLoaded = LoadTextureFromMemory(
		    g_srtLogo.data(),
		    hookState.logoWidth,
		    hookState.logoHeight,
		    hookState.device,
		    hookState.logoHandle.cpu,
		    &hookState.logoTexture);
		if (!logoLoaded)
		{
			logger->LogMessage("DX12Hook::Initialize() - LoadTextureFromMemory failed.\n");
			return E_FAIL;
		}

		hookState.origWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hookState.gameWindow, GWLP_WNDPROC, hkWndProc));

		hookState.initialized = true;
		logger->LogMessage("DX12Hook::Initialize() - completed successfully.\n");

		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE DX12Hook::Release() // Detach hooks and release resources.
	{
		HRESULT hResult = S_OK;

		if (hookState.origWndProc)
		{
			SetWindowLongPtrW(hookState.gameWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(hookState.origWndProc));
			hookState.origWndProc = nullptr;
		}

		if (hookState.fence && hookState.commandQueue)
		{
			hookState.fenceValue++;
			hResult = hookState.commandQueue->Signal(hookState.fence.Get(), hookState.fenceValue);
			if (hookState.fence->GetCompletedValue() < hookState.fenceValue)
			{
				hResult = hookState.fence->SetEventOnCompletion(hookState.fenceValue, hookState.fenceEvent);
				WaitForSingleObject(hookState.fenceEvent, 5000); // 5s timeout
			}
		}

		DestroyTexture(&hookState.logoTexture);

		hookState.commandList.Reset();
		for (auto &frameContext : hookState.frameContexts)
		{
			frameContext.commandAllocator.Reset();
			frameContext.renderTarget.Reset();
		}
		hookState.heaps.Reset();
		hookState.fence.Reset();
		if (hookState.fenceEvent)
		{
			CloseHandle(hookState.fenceEvent);
			hookState.fenceEvent = nullptr;
		}
		hookState.commandQueue = nullptr;
		hookState.device = nullptr;

		hookState.initialized = false;
		return hResult;
	}
}
