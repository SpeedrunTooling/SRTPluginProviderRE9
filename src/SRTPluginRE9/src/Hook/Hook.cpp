#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Hook.h"
#include "AOBScanner.h"
#include "CompositeOrderer.h"
#include "DeferredWndProc.h"
#include "Globals.h"
#include "Render.h"
#include "Settings.h"
#include "UI.h"
#include "imgui_impl_win32.h"
#include <algorithm>
#include <cinttypes>
#include <functional>
#include <mutex>
#include <optional>
#include <ranges>

constinit uint32_t memoryReadIntervalInMS = 16U;

std::optional<std::uintptr_t> g_BaseAddress;
inline DX12HookState g_dx12HookState{};
std::unique_ptr<SRTPluginRE9::Hook::UI> srtUI;

inline std::atomic<uint32_t> g_framesSinceInit = 0;
constexpr uint32_t framesUntilInit = 5 * 4; // 5 times back buffer count.
inline std::atomic lastDeviceStatus = S_OK;
inline std::atomic g_skipRenderingOnInit = true;
inline std::atomic<bool> g_shutdownRequested = false;

inline std::mutex g_queueMutex;
inline ID3D12CommandQueue *g_lastSeenDirectQueue = nullptr;

// SRTPluginRE9::Hook::DescriptorHandle imguiFontHandle;
inline std::atomic g_firstRunPresent = true;

DeferredWndProc g_DeferredWndProc;
SRTSettings g_SRTSettings;

namespace SRTPluginRE9::Hook
{
	[[nodiscard]] std::optional<std::uintptr_t> get_module_base(const wchar_t *module_name) noexcept
	{
		__try
		{
			auto module = GetModuleHandleW(module_name);
			if (module == nullptr)
				return std::nullopt;
			return reinterpret_cast<std::uintptr_t>(module);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return std::nullopt;
		}
	}

	LRESULT CALLBACK hkWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		g_DeferredWndProc.Enqueue(hwnd, msg, wParam, lParam);

		if (msg == WM_KEYDOWN)
		{
			if (wParam == VK_F7)
			{
				srtUI->ToggleUI();
				logger->LogMessage("Hook - F7 pressed, toggling UI...\n");
			}
			else if (wParam == VK_F8)
			{
				logger->LogMessage("Hook - F8 pressed, shutting down...\n");
				g_shutdownRequested.store(true);
			}
		}

		const auto &imguiIO = ImGui::GetIO();

		if (imguiIO.WantCaptureKeyboard)
		{
			switch (msg)
			{
				case WM_KEYDOWN:
				case WM_KEYUP:
				case WM_SYSKEYDOWN:
				case WM_SYSKEYUP:
				case WM_CHAR:
				case WM_SYSCHAR:
					return true;
				default:
					break;
			}
		}

		if (imguiIO.WantCaptureMouse)
		{
			switch (msg)
			{
				case WM_LBUTTONDOWN:
				case WM_LBUTTONUP:
				case WM_LBUTTONDBLCLK:
				case WM_RBUTTONDOWN:
				case WM_RBUTTONUP:
				case WM_RBUTTONDBLCLK:
				case WM_MBUTTONDOWN:
				case WM_MBUTTONUP:
				case WM_MBUTTONDBLCLK:
				case WM_XBUTTONDOWN:
				case WM_XBUTTONUP:
				case WM_XBUTTONDBLCLK:
				case WM_MOUSEMOVE:
				case WM_MOUSEWHEEL:
				case WM_MOUSEHWHEEL:
					return true;
				default:
					break;
			}
		}

		// Handle WM_INPUT separately — it carries both mouse and keyboard raw data
		if (msg == WM_INPUT)
		{
			UINT size = 0;
			GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));

			if (size > 0 && size <= 256)
			{
				alignas(RAWINPUT) BYTE buf[256];
				if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, buf, &size, sizeof(RAWINPUTHEADER)) == size)
				{
					auto *raw = reinterpret_cast<RAWINPUT *>(buf);

					if (raw->header.dwType == RIM_TYPEKEYBOARD && imguiIO.WantCaptureKeyboard)
						return true;

					if (raw->header.dwType == RIM_TYPEMOUSE && imguiIO.WantCaptureMouse)
						return true;
				}
			}
		}

		return CallWindowProcW(g_dx12HookState.origWndProc, hwnd, msg, wParam, lParam);
	}

	static bool IsKeyboardDevice(IDirectInputDevice8W *pDevice)
	{
		DIDEVICEINSTANCEW inst{};
		inst.dwSize = sizeof(inst);
		if (SUCCEEDED(pDevice->GetDeviceInfo(&inst)))
			return GET_DIDEVICE_TYPE(inst.dwDevType) == DI8DEVTYPE_KEYBOARD;
		return false;
	}

	HRESULT CALLBACK hkGetDeviceState(IDirectInputDevice8W *pDevice, DWORD cbData, LPVOID lpvData)
	{
		HRESULT hr = oGetDeviceState(pDevice, cbData, lpvData);
		if (SUCCEEDED(hr) && lpvData && ImGui::GetCurrentContext())
		{
			if (ImGui::GetIO().WantCaptureKeyboard && IsKeyboardDevice(pDevice))
			{
				// Zero out the entire keyboard state buffer
				memset(lpvData, 0, cbData);
			}
		}
		return hr;
	}

	HRESULT CALLBACK hkGetDeviceData(IDirectInputDevice8W *pDevice, DWORD cbObjectData, LPDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD dwFlags)
	{
		HRESULT hr = oGetDeviceData(pDevice, cbObjectData, rgdod, pdwInOut, dwFlags);
		if (SUCCEEDED(hr) && ImGui::GetCurrentContext())
		{
			if (ImGui::GetIO().WantCaptureKeyboard && IsKeyboardDevice(pDevice))
			{
				// Report zero events — swallow all buffered keyboard data
				if (pdwInOut)
					*pdwInOut = 0;
			}
		}
		return hr;
	}

	bool initImGui(IDXGISwapChain3 *pSwapChain)
	{
		HRESULT hResult = S_OK;
		static_assert(sizeof(ImTextureID) >= sizeof(D3D12_CPU_DESCRIPTOR_HANDLE), "D3D12_CPU_DESCRIPTOR_HANDLE is too large to fit in an ImTextureID");

		if (FAILED(hResult = pSwapChain->GetDevice(IID_PPV_ARGS(&g_dx12HookState.device))))
		{
			logger->LogMessage("initImGui() - GetDevice failed: {:#x}\n", static_cast<uint32_t>(hResult));
			return false;
		}

		logger->LogMessage("initImGui() - Using command queue: {:p}\n",
		                   reinterpret_cast<void *>(g_dx12HookState.commandQueue));

		DXGI_SWAP_CHAIN_DESC desc{};
		pSwapChain->GetDesc(&desc);
		g_dx12HookState.bufferCount = desc.BufferCount;
		g_dx12HookState.gameWindow = desc.OutputWindow;
		g_dx12HookState.frameContexts.resize(g_dx12HookState.bufferCount);

		auto backBufferFormat = desc.BufferDesc.Format;
		logger->LogMessage("initImGui() - BufferCount={}, Format={}, Window={:p}\n",
		                   g_dx12HookState.bufferCount,
		                   static_cast<uint32_t>(backBufferFormat),
		                   reinterpret_cast<void *>(g_dx12HookState.gameWindow));

		UINT rtvCapacity = g_dx12HookState.bufferCount;
		UINT srvCapacity = 64; // (1 ImGui font + n textures)
		logger->LogMessage("initImGui() - Allocating RTV ({}) and CBV, SRV, UAV ({}) Heaps\n", rtvCapacity, srvCapacity);

		auto heapResult = g_dx12HookState.heaps.Init(g_dx12HookState.device.Get(),
		                                             rtvCapacity,
		                                             srvCapacity);
		if (!heapResult)
		{
			logger->LogMessage("initImGui() - Heap init failed: {}\n", heapResult.error());

			return false;
		}

		for (UINT i = 0; i < g_dx12HookState.bufferCount; ++i)
		{
			auto &frameContext = g_dx12HookState.frameContexts[i];
			auto rtvHandle = g_dx12HookState.heaps.rtv.Allocate();
			frameContext.rtvHandle = rtvHandle.cpu;

			if (FAILED(hResult = pSwapChain->GetBuffer(i, IID_PPV_ARGS(&frameContext.renderTarget))))
			{
				logger->LogMessage("initImGui() - GetBuffer failed: {:#x}\n", static_cast<uint32_t>(hResult));

				return false;
			}

			D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{
			    .Format = backBufferFormat,
			    .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D,
			};

			g_dx12HookState.device->CreateRenderTargetView(frameContext.renderTarget.Get(), &rtvDesc, rtvHandle.cpu);
			if (FAILED(hResult = g_dx12HookState.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frameContext.commandAllocator))))
			{
				logger->LogMessage("initImGui() - CreateCommandAllocator failed: {:#x}\n", static_cast<uint32_t>(hResult));

				return false;
			}
		}

		if (FAILED(hResult = g_dx12HookState.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_dx12HookState.frameContexts[0].commandAllocator.Get(), nullptr, IID_PPV_ARGS(&g_dx12HookState.commandList))))
		{
			logger->LogMessage("initImGui() - CreateCommandList failed: {:#x}\n", static_cast<uint32_t>(hResult));

			return false;
		}
		g_dx12HookState.commandList->Close();

		ImGui_ImplWin32_EnableDpiAwareness();

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		RegisterSRTSettingsHandler();
		ImGui::StyleColorsDark();

		ImGuiIO &io = ImGui::GetIO();
		io.ConfigFlags = ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
		io.IniFilename = "SRTRE9_ImGui.ini";
		io.LogFilename = "SRTRE9_ImGui.log";

		// Setup scaling
		ImGuiStyle &style = ImGui::GetStyle();
		auto mainScale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));
		style.ScaleAllSizes(mainScale);
		style.FontScaleDpi = mainScale;
		style.FontSizeBase = 16.0f;
		io.Fonts->AddFontDefaultVector();

		ImGui_ImplWin32_Init(g_dx12HookState.gameWindow);

		ImGui_ImplDX12_InitInfo init_info = {};
		init_info.Device = g_dx12HookState.device.Get();
		init_info.CommandQueue = g_dx12HookState.commandQueue;
		init_info.NumFramesInFlight = static_cast<int>(g_dx12HookState.bufferCount);
		init_info.RTVFormat = backBufferFormat;
		init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;
		// Allocating SRV descriptors (for textures) is up to the application, so we provide callbacks.
		// (current version of the backend will only allocate one descriptor, future versions will need to allocate more)
		init_info.SrvDescriptorHeap = g_dx12HookState.heaps.srv.GetHeap();
		init_info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo *, D3D12_CPU_DESCRIPTOR_HANDLE *out_cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE *out_gpu_handle)
		{
			auto allocHandles = g_dx12HookState.heaps.srv.Allocate();
			*out_cpu_handle = allocHandles.cpu;
			*out_gpu_handle = allocHandles.gpu;
		};
		init_info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo *, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle)
		{
			g_dx12HookState.heaps.srv.Free(cpu_handle.ptr, gpu_handle.ptr);
		};
		ImGui_ImplDX12_Init(&init_info);

		// Create the SRT UI class which will allocate a texture on the heap, which should happen here.
		srtUI = std::make_unique<SRTPluginRE9::Hook::UI>();
		logger->SetUIPtr(srtUI.get());

		if (FAILED(hResult = g_dx12HookState.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_dx12HookState.fence))))
		{
			logger->LogMessage("initImGui() - CreateFence failed: {:#x}\n", static_cast<uint32_t>(hResult));

			return false;
		}
		g_dx12HookState.fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);

		g_dx12HookState.origWndProc = reinterpret_cast<WNDPROC>(
		    SetWindowLongPtrW(g_dx12HookState.gameWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(hkWndProc)));

		g_dx12HookState.initialized = true;
		logger->LogMessage("initImGui() - completed successfully.\n");

		return true;
	}

	HRESULT STDMETHODCALLTYPE hkPresent(IDXGISwapChain3 *pSwapChain, UINT SyncInterval, UINT Flags)
	{
		if (g_shutdownRequested.load())
			return oPresent(pSwapChain, SyncInterval, Flags);

		// Wait for enough frames so we've seen the game's command queues
		if (g_framesSinceInit.fetch_add(1) < framesUntilInit)
			return oPresent(pSwapChain, SyncInterval, Flags);

		if (!g_dx12HookState.initialized)
		{
			// Snapshot the last DIRECT queue seen before this Present call.
			// This is the game's rendering queue — the one that just submitted
			// the final frame commands right before calling Present.
			{
				std::lock_guard lock(g_queueMutex);
				if (!g_lastSeenDirectQueue)
					return oPresent(pSwapChain, SyncInterval, Flags);
				g_dx12HookState.commandQueue = g_lastSeenDirectQueue;
			}

			logger->LogMessage("hkPresent() - Captured queue {:p} (last DIRECT queue before Present)\n",
			                   reinterpret_cast<void *>(g_dx12HookState.commandQueue));

			if (!initImGui(pSwapChain))
			{
				g_dx12HookState.commandQueue = nullptr; // reset on failure
				return oPresent(pSwapChain, SyncInterval, Flags);
			}

			g_skipRenderingOnInit.store(true);
			return oPresent(pSwapChain, SyncInterval, Flags);
		}

		// Also skip the second render after init, just in case.
		if (g_skipRenderingOnInit.exchange(false))
			return oPresent(pSwapChain, SyncInterval, Flags);

		// Bail if device is already in an error state
		auto deviceStatus = g_dx12HookState.device->GetDeviceRemovedReason();
		if (deviceStatus != S_OK)
		{
			if (deviceStatus != lastDeviceStatus.exchange(deviceStatus))
				logger->LogMessage("Hook - Device lost (reason: {:#x})\n", static_cast<uint32_t>(deviceStatus));
			return oPresent(pSwapChain, SyncInterval, Flags);
		}

		// Get the current frame context.
		auto frameIndex = pSwapChain->GetCurrentBackBufferIndex();
		auto &frameContext = g_dx12HookState.frameContexts[frameIndex];

		// Wait for this allocator to be free.
		// This only blocks if the GPU hasn't finished the last time we used this allocator.
		if (g_dx12HookState.fence->GetCompletedValue() < frameContext.fenceValue)
		{
			g_dx12HookState.fence->SetEventOnCompletion(frameContext.fenceValue, g_dx12HookState.fenceEvent);
			WaitForSingleObject(g_dx12HookState.fenceEvent, INFINITE);
		}

		// Should be safe to reset this allocator now.
		if (FAILED(frameContext.commandAllocator->Reset()))
			return oPresent(pSwapChain, SyncInterval, Flags);

		if (FAILED(g_dx12HookState.commandList->Reset(frameContext.commandAllocator.Get(), nullptr)))
			return oPresent(pSwapChain, SyncInterval, Flags);

		const auto firstRun = g_firstRunPresent.exchange(false);

		g_DeferredWndProc.ProcessQueue();

		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		srtUI->DrawUI();

		ImGui::EndFrame();
		ImGui::Render();

		if (firstRun)
		{
			logger->LogMessage("hkPresent() - frameIndex={}, renderTarget={:p}, commandQueue={:p}\n",
			                   frameIndex,
			                   reinterpret_cast<void *>(frameContext.renderTarget.Get()),
			                   reinterpret_cast<void *>(g_dx12HookState.commandQueue));

			// Check if renderTarget is valid
			if (!frameContext.renderTarget)
				logger->LogMessage("hkPresent() - WARNING: renderTarget is null!\n");

			// Log the device removed reason BEFORE we do anything
			auto preStatus = g_dx12HookState.device->GetDeviceRemovedReason();
			logger->LogMessage("hkPresent() - Device status before render: {:#x}\n",
			                   static_cast<uint32_t>(preStatus));
		}

		// Get barrier and draw.
		D3D12_RESOURCE_BARRIER barrier{
		    .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
		    .Transition = {
		        .pResource = frameContext.renderTarget.Get(),
		        .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
		        .StateBefore = D3D12_RESOURCE_STATE_PRESENT,
		        .StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET,
		    },
		};
		g_dx12HookState.commandList->ResourceBarrier(1, &barrier);
		g_dx12HookState.commandList->OMSetRenderTargets(1, &frameContext.rtvHandle, FALSE, nullptr);

		ID3D12DescriptorHeap *heaps[] = {g_dx12HookState.heaps.srv.GetHeap()};
		g_dx12HookState.commandList->SetDescriptorHeaps(1, heaps);
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_dx12HookState.commandList.Get());

		std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
		g_dx12HookState.commandList->ResourceBarrier(1, &barrier);

		if (FAILED(g_dx12HookState.commandList->Close()))
			return oPresent(pSwapChain, SyncInterval, Flags);

		ID3D12CommandList *ppLists[] = {g_dx12HookState.commandList.Get()};
		g_dx12HookState.commandQueue->ExecuteCommandLists(1, ppLists);

		// Signal the fence for this frame context.
		// Next time this buffer index comes around, we'll wait on this value.
		g_dx12HookState.fenceValue++;
		frameContext.fenceValue = g_dx12HookState.fenceValue;
		g_dx12HookState.commandQueue->Signal(g_dx12HookState.fence.Get(), g_dx12HookState.fenceValue);

		auto presentResult = oPresent(pSwapChain, SyncInterval, Flags);
		if (firstRun)
		{
			auto postStatus = g_dx12HookState.device->GetDeviceRemovedReason();
			logger->LogMessage("hkPresent() - oPresent returned {:#x}, device status after: {:#x}\n",
			                   static_cast<uint32_t>(presentResult),
			                   static_cast<uint32_t>(postStatus));
		}
		return presentResult;
	}

	HRESULT STDMETHODCALLTYPE hkResizeBuffers(IDXGISwapChain *pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT Flags)
	{
		HRESULT hResult = S_OK;

		if (!g_dx12HookState.initialized)
			return oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, Flags);

		ID3D12Device *device;
		ID3D12Device *cachedDevice = (ID3D12Device *) (*g_dx12HookState.device.GetAddressOf());
		assert(!FAILED(pSwapChain->GetDevice(IID_PPV_ARGS(&device))));

		// NOTE(@j): This line catches invalidation of the device object
		//  which may occur through certain edge cases on some drivers. 
		// If this assert is ever caught we might need to handle things accordingly.
#if _DEBUG
		assert(device == cachedDevice);
#else
		(void)(cachedDevice);
#endif

		ImGui_ImplDX12_InvalidateDeviceObjects();

		// Release render targets before the resize
		for (auto &frameContext : g_dx12HookState.frameContexts)
		{
			frameContext.renderTarget.Reset();
		}

		if (FAILED(hResult = oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, Flags)))
		{
			logger->LogMessage("hkResizeBuffers() - oResizeBuffers failed: {:#x}\n", static_cast<uint32_t>(hResult));

			return hResult;
		}

		DXGI_SWAP_CHAIN_DESC desc{};
		if (FAILED(hResult = pSwapChain->GetDesc(&desc)))
		{
			logger->LogMessage("hkResizeBuffers() - GetDesc failed: {:#x}\n", static_cast<uint32_t>(hResult));

			return hResult;
		}

		// NOTE(@j): 
		// We must recreate RTV descriptors on resize.
		// Previously RTV descriptor heap kept being allocated from and was never reset
		// while using a linear allocator, producing invalid descriptor handles which 
		// caused dereferences to invalid descriptors in the graphics driver and 
		// descriptor exhaustion over time (memory leak!).
		//
		// It's really important to fully reset and reinitialize the RTV heap here, 
		// then allocate one RTV descriptor per back buffer as usual.
		auto &rtv = g_dx12HookState.heaps.rtv;
		g_dx12HookState.bufferCount = desc.BufferCount;
		rtv.Reset();

		UINT rtvCapacity = g_dx12HookState.bufferCount;
		logger->LogMessage("hkResizeBuffers() - Reallocating RTV ({}) Heaps\n", rtvCapacity);
		auto rtvResult = rtv.Init(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, rtvCapacity, false);
		if (!rtvResult) {
			auto error = rtvResult.error();
			logger->LogMessage("hkResizeBuffers() - Failed recreating RTV descriptor heaps with error: {}\n", error);
			hResult = E_FAIL;
			return hResult;
		}

		// Recreate render targets
		for (UINT i = 0; i < g_dx12HookState.bufferCount; ++i)
		{
			auto &frameContext = g_dx12HookState.frameContexts[i];

			auto rtvHandle = g_dx12HookState.heaps.rtv.Allocate();
			frameContext.rtvHandle = rtvHandle.cpu;

			if (FAILED(hResult = pSwapChain->GetBuffer(i, IID_PPV_ARGS(&frameContext.renderTarget))))
			{
				logger->LogMessage("initImGui() - GetBuffer failed: {:#x}\n", static_cast<uint32_t>(hResult));

				return false;
			}

			device->CreateRenderTargetView(frameContext.renderTarget.Get(), 0, rtvHandle.cpu);
		}

		ImGui_ImplDX12_CreateDeviceObjects();

		srtUI->DesktopResized();

		return hResult;
	}

	void STDMETHODCALLTYPE hkExecuteCommandLists(
	    ID3D12CommandQueue *pQueue, UINT NumCommandLists,
	    ID3D12CommandList *const *ppCommandLists)
	{
		// Continuously track the most recent DIRECT queue until we lock one in.
		if (!g_dx12HookState.commandQueue)
		{
			D3D12_COMMAND_QUEUE_DESC desc = pQueue->GetDesc();
			if (desc.Type == D3D12_COMMAND_LIST_TYPE_DIRECT)
			{
				std::lock_guard lock(g_queueMutex);
				g_lastSeenDirectQueue = pQueue;
			}
		}

		oExecuteCommandLists(pQueue, NumCommandLists, ppCommandLists);
	}

	bool Hook::Startup()
	{
		auto retVal = true;
		logger->LogMessage("Hook::Startup() called.\n");

		g_BaseAddress = get_module_base(L"re9.exe");
		if (!g_BaseAddress.has_value())
		{
			logger->LogMessage("Hook::Startup() Base address not found!\n");
			return false;
		}
		logger->LogMessage("Hook::Startup() Base address: {}\n", g_BaseAddress.value());

		const auto vtables = SetVTables();
		if (!vtables)
		{
			logger->LogMessage("VTable recovery failed: {}\n", vtables.error());
			return false;
		}

		if (!vtables->present || !vtables->resizeBuffers || !vtables->executeCommandLists || !vtables->getDeviceState || !vtables->getDeviceData)
		{
			logger->LogMessage("Hook::Startup() VTable pointer(s) are null!\n");
			return false;
		}

		// Give the driver a moment to fully clean up our
		// dummy device/swapchain before we start hooking
		// the vtable functions.
		Sleep(100);

		auto status = MH_Initialize();

		MH_CreateHook(vtables->present, reinterpret_cast<void *>(&hkPresent),
		              reinterpret_cast<void **>(&oPresent));
		MH_CreateHook(vtables->resizeBuffers, reinterpret_cast<void *>(&hkResizeBuffers),
		              reinterpret_cast<void **>(&oResizeBuffers));
		MH_CreateHook(vtables->executeCommandLists, reinterpret_cast<void *>(&hkExecuteCommandLists),
		              reinterpret_cast<void **>(&oExecuteCommandLists));
		MH_CreateHook(vtables->getDeviceState, reinterpret_cast<void *>(&hkGetDeviceState),
		              reinterpret_cast<void **>(&oGetDeviceState));
		MH_CreateHook(vtables->getDeviceData, reinterpret_cast<void *>(&hkGetDeviceData),
		              reinterpret_cast<void **>(&oGetDeviceData));

		MH_EnableHook(MH_ALL_HOOKS);
		retVal = status == MH_OK;

		logger->LogMessage("Hook::Startup() exiting: {:d}\n", retVal);

		return retVal;
	}

	void Hook::Shutdown()
	{
		logger->LogMessage("Hook::Shutdown() called.\n");

		MH_DisableHook(MH_ALL_HOOKS);
		MH_Uninitialize();

		Sleep(100);

		if (g_dx12HookState.origWndProc)
		{
			SetWindowLongPtrW(g_dx12HookState.gameWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_dx12HookState.origWndProc));
			g_dx12HookState.origWndProc = nullptr;
		}

		if (g_dx12HookState.fence && g_dx12HookState.commandQueue)
		{
			g_dx12HookState.fenceValue++;
			g_dx12HookState.commandQueue->Signal(g_dx12HookState.fence.Get(), g_dx12HookState.fenceValue);
			if (g_dx12HookState.fence->GetCompletedValue() < g_dx12HookState.fenceValue)
			{
				g_dx12HookState.fence->SetEventOnCompletion(g_dx12HookState.fenceValue, g_dx12HookState.fenceEvent);
				WaitForSingleObject(g_dx12HookState.fenceEvent, 5000); // 5s timeout
			}
		}

		ImGui_ImplDX12_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();

		logger->SetUIPtr(nullptr);
		srtUI.reset();

		g_dx12HookState.commandList.Reset();
		for (auto &frameContext : g_dx12HookState.frameContexts)
		{
			frameContext.commandAllocator.Reset();
			frameContext.renderTarget.Reset();
		}
		g_dx12HookState.heaps.Reset();
		g_dx12HookState.fence.Reset();
		if (g_dx12HookState.fenceEvent)
		{
			CloseHandle(g_dx12HookState.fenceEvent);
			g_dx12HookState.fenceEvent = nullptr;
		}
		g_dx12HookState.commandQueue = nullptr;
		// Do NOT release g_dx12HookState.device - they belong to the game

		g_dx12HookState.initialized = false;
		logger->LogMessage("Hook::Shutdown() exiting...\n");
	}

	Hook &Hook::GetInstance()
	{
		logger->LogMessage("Hook::GetInstance() called.\n");

		static Hook instance;
		return instance;
	}

	DWORD WINAPI Hook::ThreadMain([[maybe_unused]] LPVOID lpThreadParameter)
	{
		auto retVal = DWORD(0);
		logger->LogMessage("Hook::ThreadMain() called.\n");

		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

		if (!Hook::GetInstance().Startup())
		{
			retVal = 0xDEAD0001;
			logger->LogMessage("Hook::ThreadMain() exiting (Hook::Startup() failed): {:d}\n", retVal);

			return retVal;
		}

		// Read until shutdown requested.
		auto rankManager = protect(reinterpret_cast<RankManager **>(*g_BaseAddress + 0x0E815400ULL)).deref();
		auto characterManager = protect(reinterpret_cast<CharacterManager **>(*g_BaseAddress + 0x0E843CF8ULL)).deref();
		auto gameClock = protect(reinterpret_cast<GameClock **>(*g_BaseAddress + 0x0E834680ULL)).deref();
		// auto cameraSystem = protect(reinterpret_cast<CameraSystem **>(*g_BaseAddress + 0x0E816138ULL)).deref();
		while (!g_shutdownRequested.load())
		{
			// Acquire an index to buffer data write into and get a reference to that data buffer.
			auto writeIndex = 1U - g_GameDataBufferReadIndex.load(std::memory_order_acquire);
			auto &localGameData = g_GameDataBuffers[writeIndex];

			// DA
			auto activeRankProfile = rankManager.follow(&RankManager::_ActiveRankProfile);
			localGameData.Data.DARank = activeRankProfile.read(&RankProfile::_CurrentRank);
			localGameData.Data.DAScore = activeRankProfile.read(&RankProfile::_RankPoints);

			// Player HP
			auto activePlayerContext = characterManager.follow(&CharacterManager::PlayerContextFast);
			auto playerHitPoint = activePlayerContext.follow(&PlayerContext::HitPoint);
			auto playerHitPointData = playerHitPoint.follow(&HitPoint::HitPointData);
			localGameData.Data.PlayerHP.CurrentHP = playerHitPointData.read(&CharacterHitPointData::_CurrentHP);
			localGameData.Data.PlayerHP.MaximumHP = playerHitPointData.read(&CharacterHitPointData::_CurrentMaxHP);
			localGameData.Data.PlayerHP.IsSetup = playerHitPointData.read(&CharacterHitPointData::_IsSetuped);

			// Enemy HP
			auto enemyContextManagedList = characterManager.follow(&CharacterManager::EnemyContextList);
			localGameData.AllEnemiesBacking = std::span(enemyContextManagedList->begin(), enemyContextManagedList->end()) |
			                                  std::views::transform([](const EnemyContext *enemyContext)
			                                                        {
				                                              auto protectedEnemyContext = protect(enemyContext);
				                                              auto hitPointData = protectedEnemyContext.follow(&EnemyContext::HitPoint).follow(&HitPoint::HitPointData);
				                                              return EnemyData
				                                              {
				                                              	.KindID = protectedEnemyContext.read(&EnemyContext::KindID),
				                                              	.HP = HPData
				                                              	{
				                                              		.CurrentHP = hitPointData.read(&CharacterHitPointData::_CurrentHP),
				                                              		.MaximumHP = hitPointData.read(&CharacterHitPointData::_CurrentMaxHP),
				                                              		.IsSetup = hitPointData.read(&CharacterHitPointData::_IsSetuped) != 0
				                                              	},
				                                              	.Position = PositionalData{}
				                                              }; }) |
			                                  std::ranges::to<std::vector>();

			localGameData.Data.AllEnemies = InteropArray{
			    .Size = localGameData.AllEnemiesBacking.size(),
			    .Values = localGameData.AllEnemiesBacking.data()};

			localGameData.FilteredEnemiesBacking = localGameData.AllEnemiesBacking |
			                                       std::views::filter([](const EnemyData &enemyData)
			                                                          { return enemyData.HP.MaximumHP >= 2 && enemyData.HP.CurrentHP != 0; }) |
			                                       std::ranges::to<std::vector>();

			constexpr auto compare = OrderByDescending([](const EnemyData &enemyData)
			                                 { return enemyData.HP.CurrentHP < enemyData.HP.MaximumHP; })
			                   .ThenByDescending([](const EnemyData &enemyData)
			                                     { return enemyData.HP.MaximumHP; });
			std::ranges::sort(localGameData.FilteredEnemiesBacking, compare);

			localGameData.Data.FilteredEnemies = InteropArray{
			    .Size = localGameData.FilteredEnemiesBacking.size(),
			    .Values = localGameData.FilteredEnemiesBacking.data()};

			//// IGT
			// auto allTimersVector = std::span<Time *>(
			//                            gameClock
			//                                .follow(&GameClock::_Timers)
			//                                .read(&ManagedArray<Time *>::_Values),
			//                            gameClock
			//                                .read(&ManagedArray<Time *>::_Count)) |
			//                        std::views::transform([](Time *time)
			//                                              { return (time) ? time->_ElapsedTime : 0ULL; }) |
			//                        std::ranges::to<std::vector>();

			// localGameData.RunningTimers = gameClock.read(&GameClock::_RunningTimers);
			// localGameData.InGameTimers = InteropArray{
			//     .Size = allTimersVector.size(),
			//     .Values = allTimersVector.data()};

			// Release this index back to the data buffer.
			g_GameDataBufferReadIndex.store(writeIndex, std::memory_order_release);

			// Sleep until next read operation.
			Sleep(memoryReadIntervalInMS);
		}

		logger->LogMessage("Hook::ThreadMain() Shutdown request received.\n");

		Hook::GetInstance().Shutdown();

		logger->LogMessage("Hook::ThreadMain() exiting: {:d}\n", retVal);

		FreeLibraryAndExitThread(g_dllModule, retVal);
	}

	[[nodiscard]] auto Hook::SetVTables() -> std::expected<VTableAddresses, std::string>
	{
		logger->LogMessage("Hook::SetVTables() called.\n");

		auto result = VTableResolver::Resolve();
		if (!result)
		{
			logger->LogMessage("Hook::SetVTables() failed: {}\n", result.error());

			return std::unexpected(result.error());
		}

		return *result;
	}
}
