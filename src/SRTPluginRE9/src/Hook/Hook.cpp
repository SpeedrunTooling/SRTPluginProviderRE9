#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Hook.h"
#include "CompositeOrderer.h"
#include "GameVersion.h"
#include "Globals.h"
#include "Render.h"
#include "Settings.h"
#include "Thread.h"
#include "UI.h"
#include <algorithm>
#include <cfloat>
#include <cinttypes>
#include <cstring>
#include <format>
#include <functional>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>
#include <mutex>
#include <optional>
#include <ranges>

constinit uint32_t memoryReadIntervalInMS = 16U;

std::optional<std::uintptr_t> g_BaseAddress;
std::unique_ptr<SRTPluginRE9::Hook::UI> srtUI;

inline std::atomic<uint32_t> g_framesSinceInit = 0;
constexpr uint32_t framesUntilInit = 5 * 4; // 5 times back buffer count.
inline std::atomic lastDeviceStatus = S_OK;
inline std::atomic g_skipRenderingOnInit = true;
inline std::atomic<bool> g_shutdownRequested = false;

inline std::mutex g_queueMutex;
inline ID3D12CommandQueue *g_lastSeenDirectQueue = nullptr;

inline std::atomic g_firstRunPresent = true;
inline std::atomic g_firstRunLockScene = true;
inline std::atomic g_firstRunUpdateBehavior = true;

SRTSettings g_SRTSettings;
SRTPluginRE9::GameVersion::GameVersion g_GameVersion;

uintptr_t getFOVOffsetPtr;
uintptr_t getCameraFOVIDOffsetPtr;
uintptr_t getModeOffsetPtr;
uintptr_t getTypeOffsetPtr;

// Render-only FOV decouple. These two are absolute addresses the via-runtime object (NOT *g_BaseAddress + offset);
// they are not reachable from the module base. Set per game version in MainLoop(). If a version's values are
// unknown (left 0) the render-FOV feature simply stays off (ResolveAppEntry() finds nothing -> no hooks).
uintptr_t viaApplicationPtr;
uintptr_t viaSceneManagerPtr;

SafetyHookInline oGetFOV{};
SafetyHookInline oLockScene{};
SafetyHookInline oUpdateBehavior{};

// Desired render FOV for the current mode/type (computed in hkGetFOV) and the gameplay FOV captured at
// LockScene so it can be restored before the next UpdateBehavior. Only the render projection sees the wide FOV.
std::atomic<float> g_DesiredRenderFOV = 0.0f;
float g_SavedGameplayFOV = 0.0f;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace SRTPluginRE9::Hook
{
	class CameraFOVCatalogUserData_ID
	{
	public:
		app::PlayerMode get_Mode(ManagedContext *ctx)
		{
			using fn_t = app::PlayerMode (*)(ManagedContext *, CameraFOVCatalogUserData_ID *);
			static const auto fn = reinterpret_cast<fn_t>(*g_BaseAddress + getModeOffsetPtr);
			return fn(ctx, this);
		}

		app::PlayerCameraFOVParam::TemplateType get_Type(ManagedContext *ctx)
		{
			using fn_t = app::PlayerCameraFOVParam::TemplateType (*)(ManagedContext *, CameraFOVCatalogUserData_ID *);
			static const auto fn = reinterpret_cast<fn_t>(*g_BaseAddress + getTypeOffsetPtr);
			return fn(ctx, this);
		}
	};

	class PlayerCameraFOVCalc
	{
	public:
		CameraFOVCatalogUserData_ID *getCameraFOVID(ManagedContext *ctx)
		{
			using fn_t = CameraFOVCatalogUserData_ID *(*)(ManagedContext *, PlayerCameraFOVCalc *);
			static const auto fn = reinterpret_cast<fn_t>(*g_BaseAddress + getCameraFOVIDOffsetPtr);
			return fn(ctx, this);
		}
	};

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

	float hkGetFOV(
	    ManagedContext *ctx,                      // RCX - Runtime context
	    PlayerCameraFOVCalc *playerCameraFOVCalc) // RDX - This pointer
	{
		// Get the original (gameplay) FOV value. This is what interaction, aim, and all gameplay must keep,
		// so we ALWAYS return it unchanged below.
		auto originalValue = oGetFOV.call<float, ManagedContext *, PlayerCameraFOVCalc *>(ctx, playerCameraFOVCalc);

		// Compute the user's desired RENDER FOV for the current mode/type. We do NOT return it here; it is
		// applied only to the render projection in hkLockScene() and reverted in hkUpdateBehavior().
		float desiredRenderFOV = originalValue;

		// Call getCameraFOVID() to get a pointer to the class that has the mode and type information.
		auto *cameraFOVID = playerCameraFOVCalc->getCameraFOVID(ctx);
		if (cameraFOVID)
		{
			// Get the mode and type.
			auto cameraFOVMode = cameraFOVID->get_Mode(ctx);
			auto cameraFOVType = cameraFOVID->get_Type(ctx);

			// Pick the FOV the user desires based on the current mode and type.
			if (cameraFOVMode == app::PlayerMode::TPS)
			{
				if (cameraFOVType == app::PlayerCameraFOVParam::TemplateType::Default)
					desiredRenderFOV = g_SRTSettings.FOVTPSNormal;
				else if (cameraFOVType == app::PlayerCameraFOVParam::TemplateType::Hold)
					desiredRenderFOV = g_SRTSettings.FOVTPSADS;
			}
			else if (cameraFOVMode == app::PlayerMode::FPS)
			{
				if (cameraFOVType == app::PlayerCameraFOVParam::TemplateType::Default)
					desiredRenderFOV = g_SRTSettings.FOVFPSNormal;
				else if (cameraFOVType == app::PlayerCameraFOVParam::TemplateType::Hold)
					desiredRenderFOV = g_SRTSettings.FOVFPSADS;
			}
		}

		// Publish the desired render FOV for the LockScene/UpdateBehavior bracket; gameplay keeps originalValue.
		g_DesiredRenderFOV.store(desiredRenderFOV);
		return originalValue;
	}

	// Render-only FOV: RE Engine via.Application pipeline-entry + camera chain ---
	constexpr uintptr_t kEntryArrayOffset = 0x818ULL; // via.Application + this = entry[0]
	constexpr uintptr_t kEntryStride = 0xC8ULL;
	constexpr uintptr_t kEntryFuncOffset = 0x08ULL;        // JIT execute thunk (SafetyHook target)
	constexpr uintptr_t kEntryNameOffset = 0x10ULL;        // const char* description (null-terminated)
	constexpr uintptr_t kSceneMgrMainViewOffset = 0x88ULL; // via.SceneManager -> via.SceneView
	constexpr uintptr_t kSceneViewPrimCamOffset = 0x90ULL; // via.SceneView -> via.Camera (PrimaryCamera; 144 dec = 0x90)
	constexpr uintptr_t kCameraFovOffset = 0x38ULL;        // via.Camera fov (float)

	// Resolve the JIT execute thunk for a named pipeline entry by walking via.Application's entry array and
	// matching the description string. The thunk address is per-RUN, so this must run each launch. Returns
	// nullptr (feature stays off) if via.Application is unset/invalid or the entry is not found.
	void *ResolveAppEntry(const char *name)
	{
		__try
		{
			if (name == nullptr)
			{
				logger->LogMessage("Hook::ResolveAppEntry() Supplied name is null! Aborting.\n");
				return nullptr;
			}

			if (!viaApplicationPtr)
			{
				logger->LogMessage("Hook::ResolveAppEntry() via.Application pointer is null! Aborting search for {}.\n", name);
				return nullptr;
			}

			const uintptr_t entries = viaApplicationPtr + kEntryArrayOffset;
			for (int i = 0; i < 1024; ++i)
			{
				const uintptr_t entry = entries + static_cast<uintptr_t>(i) * kEntryStride;
				const char *desc = *reinterpret_cast<const char *const *>(entry + kEntryNameOffset);
				if (desc != nullptr && std::strcmp(desc, name) == 0)
				{
					auto fnPtr = *reinterpret_cast<void *const *>(entry + kEntryFuncOffset);
					logger->LogMessage("Hook::ResolveAppEntry() Found {} at {:p}\n", name, fnPtr);
					return fnPtr;
				}
			}

			logger->LogMessage("Hook::ResolveAppEntry() No results found for {}.\n", name);
			return nullptr;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			logger->LogMessage("Hook::ResolveAppEntry() Exception occurred searching for {}.\n", name);
			return nullptr;
		}
	}

	// via.SceneManager -> MainView (field) -> PrimaryCamera (field) -> &fov. Unguarded; callers wrap in SEH.
	float *ResolveCameraFovField()
	{
		if (!viaSceneManagerPtr)
		{
			logger->LogMessage("Hook::ResolveCameraFovField() via.SceneManager pointer is null!\n");
			return nullptr;
		}

		const uintptr_t view = *reinterpret_cast<const uintptr_t *>(viaSceneManagerPtr + kSceneMgrMainViewOffset);
		if (!view)
		{
			logger->LogMessage("Hook::ResolveCameraFovField() View pointer is null!\n");
			return nullptr;
		}

		const uintptr_t camera = *reinterpret_cast<const uintptr_t *>(view + kSceneViewPrimCamOffset);
		if (!camera)
		{
			logger->LogMessage("Hook::ResolveCameraFovField() Camera pointer is null!\n");
			return nullptr;
		}

		return reinterpret_cast<float *>(camera + kCameraFovOffset);
	}

	bool TryReadCameraFov(float &out)
	{
		__try
		{
			float *p = ResolveCameraFovField();
			if (!p)
				return false;
			out = *p;
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			logger->LogMessage("Hook::TryReadCameraFov() Exception occurred.\n");
			return false;
		}
	}

	bool TryWriteCameraFov(float value)
	{
		__try
		{
			float *p = ResolveCameraFovField();
			if (!p)
				return false;
			*p = value;
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			logger->LogMessage("Hook::TryWriteCameraFov() Exception occurred.\n");
			return false;
		}
	}

	// Runs at the LockScene pipeline stage (after all gameplay/interaction). Swap in the wide render FOV.
	void hkLockScene(void *entry)
	{
		if (g_SRTSettings.FOVEnable)
		{
			const float desired = g_DesiredRenderFOV.load();
			if (desired > 1.0f)
			{
				float current = 0.0f;
				if (TryReadCameraFov(current) && TryWriteCameraFov(desired))
					g_SavedGameplayFOV = current;
			}
		}
		oLockScene.call<void, void *>(entry);
	}

	// Runs at the UpdateBehavior pipeline stage (before gameplay/interaction). Restore the gameplay FOV.
	void hkUpdateBehavior(void *entry)
	{
		if (g_SavedGameplayFOV > 1.0f)
		{
			TryWriteCameraFov(g_SavedGameplayFOV);
			g_SavedGameplayFOV = 0.0f;
		}
		oUpdateBehavior.call<void, void *>(entry);
	}

	LRESULT CALLBACK hkWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		if (ImGui::GetCurrentContext() != nullptr)
			ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);

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

		// Protect against this section being called before ImGui context is setup.
		if (ImGui::GetCurrentContext() != nullptr)
		{
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
						return 0;
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
						return 0;
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
							return 0;

						if (raw->header.dwType == RIM_TYPEMOUSE && imguiIO.WantCaptureMouse)
							return 0;
					}
				}
			}
		}

		return CallWindowProcW(DX12Hook::DX12Hook::GetInstance().GetHookState().origWndProc, hwnd, msg, wParam, lParam);
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
		HRESULT hr = DInput8Hook::DInput8Hook::GetInstance().GetOriginalGetDeviceState()(pDevice, cbData, lpvData);
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
		HRESULT hr = DInput8Hook::DInput8Hook::GetInstance().GetOriginalGetDeviceData()(pDevice, cbObjectData, rgdod, pdwInOut, dwFlags);
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

	static bool initImGuiD3D12Backend(IDXGISwapChain3 *pSwapChain)
	{
		logger->LogMessage("initImGuiD3D12Backend() called.\n");
		auto &hookState = DX12Hook::DX12Hook::GetInstance().GetHookState();

		logger->LogMessage("initImGuiD3D12Backend() getting the DXGI SwapChain descriptor from the current SwapChain.\n");
		DXGI_SWAP_CHAIN_DESC desc{};
		pSwapChain->GetDesc(&desc);
		auto backBufferFormat = desc.BufferDesc.Format;

		logger->LogMessage("initImGuiD3D12Backend() setting up ImGui's DirectX 12 initialization info structure.\n");
		ImGui_ImplDX12_InitInfo init_info = {};
		init_info.Device = hookState.device;
		init_info.CommandQueue = hookState.commandQueue;
		init_info.NumFramesInFlight = static_cast<int>(hookState.bufferCount);
		init_info.RTVFormat = backBufferFormat;
		init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;
		init_info.SrvDescriptorHeap = hookState.heaps.srv.GetHeap();
		init_info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo *, D3D12_CPU_DESCRIPTOR_HANDLE *out_cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE *out_gpu_handle)
		{
			auto &hs = DX12Hook::DX12Hook::GetInstance().GetHookStateMut();
			auto allocHandles = hs.heaps.srv.Allocate();
			*out_cpu_handle = allocHandles.cpu;
			*out_gpu_handle = allocHandles.gpu;
		};
		init_info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo *, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle)
		{
			auto &hs = DX12Hook::DX12Hook::GetInstance().GetHookStateMut();
			hs.heaps.srv.Free(cpu_handle.ptr, gpu_handle.ptr);
		};

		logger->LogMessage("initImGuiD3D12Backend() initializing ImGui's DirectX 12 implementation.\n");
		ImGui_ImplDX12_Init(&init_info);

		logger->LogMessage("initImGuiD3D12Backend() completed successfully.\n");
		return true;
	}

	static bool initImGuiOverlay(IDXGISwapChain3 *pSwapChain)
	{
		logger->LogMessage("initImGuiOverlay() called.\n");
		auto &hookState = DX12Hook::DX12Hook::GetInstance().GetHookState();

		logger->LogMessage("initImGuiOverlay() enabling Win32 DPI awareness in ImGui.\n");
		ImGui_ImplWin32_EnableDpiAwareness();

		logger->LogMessage("initImGuiOverlay() IMGUI_CHECKVERSION() called.\n");
		IMGUI_CHECKVERSION();
		logger->LogMessage("initImGuiOverlay() creating ImGui context.\n");
		ImGui::CreateContext();
		logger->LogMessage("initImGuiOverlay() registering settings handler.\n");
		RegisterSRTSettingsHandler();

		logger->LogMessage("initImGuiOverlay() getting ImGuiIO reference and setting configuration.\n");
		ImGuiIO &io = ImGui::GetIO();
		io.ConfigFlags = ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
		io.IniFilename = "SRTRE9_ImGui.ini";
		io.LogFilename = "SRTRE9_ImGui.log";

		logger->LogMessage("initImGuiOverlay() initializing ImGui's Win32 implementation.\n");
		ImGui_ImplWin32_Init(hookState.gameWindow);

		logger->LogMessage("initImGuiOverlay() initializing DirectX 12 implementation.\n");
		if (!initImGuiD3D12Backend(pSwapChain))
			return false;

		// Create the SRT UI class which will allocate a texture on the heap, which should happen here.
		logger->LogMessage("initImGuiOverlay() initializing UI class.\n");
		srtUI = std::make_unique<SRTPluginRE9::Hook::UI>();
		logger->LogMessage("initImGuiOverlay() setting the logger's reference to the UI instance.\n");
		logger->SetUIPtr(srtUI.get());
		logger->LogMessage("initImGuiOverlay() clearing the font atlas and re-adding the default vector font.\n");
		io.Fonts->Clear();
		io.Fonts->AddFontDefaultVector();

		logger->LogMessage("initImGuiOverlay() completed successfully.\n");
		return true;
	}

	HRESULT STDMETHODCALLTYPE hkPresent(IDXGISwapChain3 *pSwapChain, UINT SyncInterval, UINT Flags)
	{
		auto &dx12 = DX12Hook::DX12Hook::GetInstance();
		auto oPresent = dx12.GetOriginalPresent();

		if (g_shutdownRequested.load())
			return oPresent(pSwapChain, SyncInterval, Flags);

		// Wait for enough frames so we've seen the game's command queues
		if (g_framesSinceInit.fetch_add(1) < framesUntilInit)
			return oPresent(pSwapChain, SyncInterval, Flags);

		auto &hookState = dx12.GetHookStateMut();

		if (!hookState.initialized)
		{
			// Snapshot the last DIRECT queue seen before this Present call.
			// This is the game's rendering queue — the one that just submitted
			// the final frame commands right before calling Present.
			{
				std::lock_guard lock(g_queueMutex);
				if (!g_lastSeenDirectQueue)
					return oPresent(pSwapChain, SyncInterval, Flags);
				hookState.commandQueue = g_lastSeenDirectQueue;
			}

			logger->LogMessage("hkPresent() Captured queue {:p} (last DIRECT queue before Present)\n",
			                   reinterpret_cast<void *>(hookState.commandQueue));

			if (FAILED(dx12.Initialize(pSwapChain, reinterpret_cast<LONG_PTR>(hkWndProc))))
			{
				hookState.commandQueue = nullptr; // reset on failure
				return oPresent(pSwapChain, SyncInterval, Flags);
			}

			// Detect reinit-after-resize vs first-time init.
			// If ImGui context already exists, only reinit the D3D12 backend + UI.
			if (ImGui::GetCurrentContext() != nullptr)
			{
				logger->LogMessage("hkPresent() Reinitializing after resize.\n");
				if (!initImGuiD3D12Backend(pSwapChain))
				{
					hookState.commandQueue = nullptr;
					return oPresent(pSwapChain, SyncInterval, Flags);
				}
				srtUI = std::make_unique<SRTPluginRE9::Hook::UI>();
				logger->SetUIPtr(srtUI.get());
				srtUI->GameWindowResized();
			}
			else
			{
				if (!initImGuiOverlay(pSwapChain))
				{
					hookState.commandQueue = nullptr;
					return oPresent(pSwapChain, SyncInterval, Flags);
				}
			}

			g_skipRenderingOnInit.store(true);
			return oPresent(pSwapChain, SyncInterval, Flags);
		}

		// Also skip the second render after init, just in case.
		if (g_skipRenderingOnInit.exchange(false))
			return oPresent(pSwapChain, SyncInterval, Flags);

		// Bail if device is already in an error state
		auto deviceStatus = hookState.device->GetDeviceRemovedReason();
		if (deviceStatus != S_OK)
		{
			if (deviceStatus != lastDeviceStatus.exchange(deviceStatus))
				logger->LogMessage("Hook - Device lost (reason: {:#x})\n", static_cast<uint32_t>(deviceStatus));
			return oPresent(pSwapChain, SyncInterval, Flags);
		}

		// Get the current frame context.
		auto frameIndex = pSwapChain->GetCurrentBackBufferIndex();
		auto &frameContext = hookState.frameContexts[frameIndex];

		// Wait for this allocator to be free.
		// This only blocks if the GPU hasn't finished the last time we used this allocator.
		if (hookState.fence->GetCompletedValue() < frameContext.fenceValue)
		{
			hookState.fence->SetEventOnCompletion(frameContext.fenceValue, hookState.fenceEvent);
			WaitForSingleObject(hookState.fenceEvent, INFINITE);
		}

		// Should be safe to reset this allocator now.
		if (FAILED(frameContext.commandAllocator->Reset()))
			return oPresent(pSwapChain, SyncInterval, Flags);

		if (FAILED(hookState.commandList->Reset(frameContext.commandAllocator.Get(), nullptr)))
			return oPresent(pSwapChain, SyncInterval, Flags);

		const auto firstRun = g_firstRunPresent.exchange(false);

		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		if (firstRun)
			UI::RescaleDPI();

		srtUI->DrawUI();

		ImGui::EndFrame();
		ImGui::Render();

		if (firstRun)
		{
			logger->LogMessage("hkPresent() frameIndex={}, renderTarget={:p}, commandQueue={:p}\n",
			                   frameIndex,
			                   reinterpret_cast<void *>(frameContext.renderTarget.Get()),
			                   reinterpret_cast<void *>(hookState.commandQueue));

			// Check if renderTarget is valid
			if (!frameContext.renderTarget)
				logger->LogMessage("hkPresent() WARNING: renderTarget is null!\n");

			// Log the device removed reason BEFORE we do anything
			auto preStatus = hookState.device->GetDeviceRemovedReason();
			logger->LogMessage("hkPresent() Device status before render: {:#x}\n",
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
		hookState.commandList->ResourceBarrier(1, &barrier);
		hookState.commandList->OMSetRenderTargets(1, &frameContext.rtvHandle, FALSE, nullptr);

		ID3D12DescriptorHeap *heaps[] = {hookState.heaps.srv.GetHeap()};
		hookState.commandList->SetDescriptorHeaps(1, heaps);
		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), hookState.commandList.Get());

		std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
		hookState.commandList->ResourceBarrier(1, &barrier);

		if (FAILED(hookState.commandList->Close()))
			return oPresent(pSwapChain, SyncInterval, Flags);

		ID3D12CommandList *ppLists[] = {hookState.commandList.Get()};
		hookState.commandQueue->ExecuteCommandLists(1, ppLists);

		// Signal the fence for this frame context.
		// Next time this buffer index comes around, we'll wait on this value.
		hookState.fenceValue++;
		frameContext.fenceValue = hookState.fenceValue;
		hookState.commandQueue->Signal(hookState.fence.Get(), hookState.fenceValue);

		auto presentResult = oPresent(pSwapChain, SyncInterval, Flags);
		if (firstRun)
		{
			auto postStatus = hookState.device->GetDeviceRemovedReason();
			logger->LogMessage("hkPresent() oPresent returned {:#x}, device status after: {:#x}\n",
			                   static_cast<uint32_t>(presentResult),
			                   static_cast<uint32_t>(postStatus));
		}
		return presentResult;
	}

	HRESULT STDMETHODCALLTYPE hkResizeBuffers(IDXGISwapChain *pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT Flags)
	{
		auto &dx12 = DX12Hook::DX12Hook::GetInstance();
		auto oResizeBuffers = dx12.GetOriginalResizeBuffers();
		auto &hookState = dx12.GetHookStateMut();

		if (!hookState.initialized)
			return oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, Flags);

		logger->LogMessage("hkResizeBuffers() Started (full teardown).\n");

		// === GPU DRAIN ===
		// Wait for all in-flight GPU work before releasing any resources.
		// Without this, ResizeBuffers can return E_ABORT because our render
		// targets are still referenced by pending GPU commands.
		if (hookState.fence && hookState.commandQueue)
		{
			hookState.fenceValue++;
			HRESULT signalHr = hookState.commandQueue->Signal(hookState.fence.Get(), hookState.fenceValue);
			if (SUCCEEDED(signalHr) && hookState.fence->GetCompletedValue() < hookState.fenceValue)
			{
				hookState.fence->SetEventOnCompletion(hookState.fenceValue, hookState.fenceEvent);
				WaitForSingleObject(hookState.fenceEvent, 5000);
			}
		}
		logger->LogMessage("hkResizeBuffers() GPU drain complete.\n");

		// === TEAR DOWN ALL OVERLAY D3D12 RESOURCES ===
		// Shut down ImGui DX12 backend (keeps ImGui context + Win32 backend alive).
		ImGui_ImplDX12_Shutdown();

		// Destroy SRT UI and logo texture (they hold D3D12 resource references).
		logger->SetUIPtr(nullptr);
		srtUI.reset();
		if (hookState.logoTexture)
			DestroyTexture(&hookState.logoTexture);
		hookState.logoHandle = {};

		// Release all D3D12 overlay resources.
		hookState.commandList.Reset();
		for (auto &fc : hookState.frameContexts)
		{
			fc.commandAllocator.Reset();
			fc.renderTarget.Reset();
			fc.fenceValue = 0;
		}
		hookState.frameContexts.clear();
		hookState.heaps.Reset();
		hookState.fence.Reset();
		if (hookState.fenceEvent)
		{
			CloseHandle(hookState.fenceEvent);
			hookState.fenceEvent = nullptr;
		}
		hookState.fenceValue = 0;
		hookState.bufferCount = 0;

		// Release device/queue references (will be re-acquired on next init).
		hookState.device = nullptr;
		hookState.commandQueue = nullptr;
		hookState.initialized = false;

		logger->LogMessage("hkResizeBuffers() Resources released, calling original.\n");

		// === CALL ORIGINAL ===
		HRESULT hResult = oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, Flags);
		if (FAILED(hResult))
			logger->LogMessage("hkResizeBuffers() oResizeBuffers failed: {:#x}\n", static_cast<uint32_t>(hResult));
		else
			logger->LogMessage("hkResizeBuffers() oResizeBuffers succeeded.\n");

		// === FORCE REINIT ON NEXT PRESENT ===
		// Clear the cached queue so hkExecuteCommandLists re-captures a fresh one.
		{
			std::lock_guard lock(g_queueMutex);
			g_lastSeenDirectQueue = nullptr;
		}
		g_firstRunPresent.store(true);
		g_skipRenderingOnInit.store(true);

		logger->LogMessage("hkResizeBuffers() Done. Will reinitialize on next Present.\n");
		return hResult;
	}

	void STDMETHODCALLTYPE hkExecuteCommandLists(
	    ID3D12CommandQueue *pQueue, UINT NumCommandLists,
	    ID3D12CommandList *const *ppCommandLists)
	{
		// Continuously track the most recent DIRECT queue until we lock one in.
		if (!DX12Hook::DX12Hook::GetInstance().GetHookState().commandQueue)
		{
			D3D12_COMMAND_QUEUE_DESC desc = pQueue->GetDesc();
			if (desc.Type == D3D12_COMMAND_LIST_TYPE_DIRECT)
			{
				std::lock_guard lock(g_queueMutex);
				g_lastSeenDirectQueue = pQueue;
			}
		}

		DX12Hook::DX12Hook::GetInstance().GetOriginalExecuteCommandLists()(pQueue, NumCommandLists, ppCommandLists);
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

		auto &dx12 = DX12Hook::DX12Hook::GetInstance();
		auto &dinput8 = DInput8Hook::DInput8Hook::GetInstance();

		// Give the driver a moment to fully clean up our
		// dummy device/swapchain before we start hooking
		// the vtable functions.
		Sleep(100);

		if (!dx12.AttachHooks(&hkPresent, &hkResizeBuffers, &hkExecuteCommandLists))
		{
			logger->LogMessage("Hook::Startup() DX12 hook attachment failed!\n");
			return false;
		}

		if (!dinput8.AttachHooks(&hkGetDeviceState, &hkGetDeviceData))
		{
			logger->LogMessage("Hook::Startup() DInput8 hook attachment failed!\n");
			return false;
		}

		logger->LogMessage("Hook::Startup() exiting: {:d}\n", retVal);

		return retVal;
	}

	void Hook::Shutdown()
	{
		logger->LogMessage("Hook::Shutdown() called.\n");

		auto &dx12 = DX12Hook::DX12Hook::GetInstance();
		auto &dinput8 = DInput8Hook::DInput8Hook::GetInstance();

		dx12.DetachHooks();
		dinput8.DetachHooks();

		Sleep(100);

		ImGui_ImplDX12_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();

		logger->SetUIPtr(nullptr);
		srtUI.reset();

		dx12.Release();

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
		HRESULT hResult;
		logger->LogMessage("Hook::ThreadMain() called.\n");

		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

		hResult = SRTPluginRE9::Thread::SetThreadName(GetCurrentThread(), std::format("{} {} Main Thread", SRTPluginRE9::GameNameShort, SRTPluginRE9::ToolNameShort));
		if (FAILED(hResult))
			logger->LogMessage("Hook::ThreadMain() failed to set thread description: {:d}\n", static_cast<uint32_t>(hResult));

		if (!Hook::GetInstance().Startup())
		{
			retVal = 0xDEAD0001;
			logger->LogMessage("Hook::ThreadMain() exiting (Hook::Startup() failed): {:d}\n", retVal);

			return retVal;
		}

		// Loops until shutdown requested. Performs memory reads and stores results in a buffer.
		MainLoop();

		logger->LogMessage("Hook::ThreadMain() Shutdown request received.\n");

		Hook::GetInstance().Shutdown();

		logger->LogMessage("Hook::ThreadMain() exiting: {:d}\n", retVal);

		FreeLibraryAndExitThread(g_dllModule, retVal);
	}

	void WINAPI Hook::MainLoop()
	{
		Protected_Ptr<RankManager> rankManager;
		Protected_Ptr<CharacterManager> characterManager;
		// Protected_Ptr<GameClock> gameClock;
		// Protected_Ptr<CameraSystem> cameraSystem;

		// Detect game version and set base pointers.
		auto gameVersion = SRTPluginRE9::GameVersion::DetectGameVersion();
		if (!gameVersion.has_value())
			logger->LogMessage("Hook::MainLoop() unable to detect game version: {}\n", gameVersion.error().c_str());

		g_GameVersion = gameVersion.value();
		switch (g_GameVersion)
		{
			default:
			case GameVersion::GameVersion::WW_20260508_1: // 1.3.0.0
			{
				logger->LogMessage("Hook::MainLoop() Game version: WW_20260508_1\n");
				rankManager = protect(reinterpret_cast<RankManager **>(*g_BaseAddress + 0x0E9E1190ULL)).deref();
				characterManager = protect(reinterpret_cast<CharacterManager **>(*g_BaseAddress + 0x0E999618ULL)).deref();
				getFOVOffsetPtr = 0x5020A0ULL;
				getCameraFOVIDOffsetPtr = 0x502620ULL;
				getModeOffsetPtr = 0x6400cf0ULL;
				getTypeOffsetPtr = 0x6404880ULL;
				break;
			}

			case GameVersion::GameVersion::WW_20260327_1: // 1.2.0.0
			{
				logger->LogMessage("Hook::MainLoop() Game version: WW_20260327_1\n");
				rankManager = protect(reinterpret_cast<RankManager **>(*g_BaseAddress + 0x0E8C7750ULL)).deref();
				characterManager = protect(reinterpret_cast<CharacterManager **>(*g_BaseAddress + 0x0E90FE10ULL)).deref();
				getFOVOffsetPtr = 0x508950ULL;
				getCameraFOVIDOffsetPtr = 0x508ED0ULL;
				getModeOffsetPtr = 0x634B400ULL;
				getTypeOffsetPtr = 0x634EF30ULL;
				break;
			}

			case GameVersion::GameVersion::WW_20260313_1: // 1.1.2.0
			{
				logger->LogMessage("Hook::MainLoop() Game version: WW_20260313_1\n");
				rankManager = protect(reinterpret_cast<RankManager **>(*g_BaseAddress + 0x0E815400ULL)).deref();
				characterManager = protect(reinterpret_cast<CharacterManager **>(*g_BaseAddress + 0x0E843CF8ULL)).deref();
				getFOVOffsetPtr = 0x52BCF0ULL;
				getCameraFOVIDOffsetPtr = 0x52C270ULL;
				getModeOffsetPtr = 0x623F7D0ULL;
				getTypeOffsetPtr = 0x6243300ULL;
				viaApplicationPtr = 0x14D3C0C0ULL;  // ABSOLUTE via.Application object (verify per-run stability)
				viaSceneManagerPtr = 0x16F992F0ULL; // ABSOLUTE via.SceneManager object
				break;
			}

			case GameVersion::GameVersion::WW_20260305_1: // 1.1.1.0
			{
				logger->LogMessage("Hook::MainLoop() Game version: WW_20260305_1\n");
				rankManager = protect(reinterpret_cast<RankManager **>(*g_BaseAddress + 0x0E816400ULL)).deref();
				characterManager = protect(reinterpret_cast<CharacterManager **>(*g_BaseAddress + 0x0E844CF8ULL)).deref();
				getFOVOffsetPtr = 0x52C300ULL;
				getCameraFOVIDOffsetPtr = 0x52C880ULL;
				getModeOffsetPtr = 0x623F7B0ULL;
				getTypeOffsetPtr = 0x62432E0ULL;
				break;
			}

			case GameVersion::GameVersion::WW_20260225_1: // 1.0.0.0
			{
				logger->LogMessage("Hook::MainLoop() Game version: WW_20260225_1\n");
				rankManager = protect(reinterpret_cast<RankManager **>(*g_BaseAddress + 0x0E857F30ULL)).deref();
				characterManager = protect(reinterpret_cast<CharacterManager **>(*g_BaseAddress + 0x0E8377C8ULL)).deref();
				getFOVOffsetPtr = 0x52E5A0ULL;
				getCameraFOVIDOffsetPtr = 0x52EB20ULL;
				getModeOffsetPtr = 0x622CBA0ULL;
				getTypeOffsetPtr = 0x62306D0ULL;
				break;
			}
		}

		// Hook version-specific hooks.
		if (getFOVOffsetPtr)
			oGetFOV = safetyhook::create_inline(reinterpret_cast<void *>(*g_BaseAddress + getFOVOffsetPtr), &hkGetFOV);

		// Render-only FOV: hook the pipeline phases by name (thunk addresses are per-run). If the version's
		// via.Application address is unknown/wrong, ResolveAppEntry() returns nullptr and these stay unhooked.
		if (void *lockSceneFn = ResolveAppEntry("LockScene"))
			oLockScene = safetyhook::create_inline(lockSceneFn, &hkLockScene);
		if (void *updateBehaviorFn = ResolveAppEntry("UpdateBehavior"))
			oUpdateBehavior = safetyhook::create_inline(updateBehaviorFn, &hkUpdateBehavior);

		// Read until shutdown requested.
		logger->LogMessage("Hook::MainLoop() Starting memory read loop indefinitely until shutdown requested...\n");
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
			auto playerIDGUID = activePlayerContext.follow(&PlayerContext::ID).read(&ContextID::_RawID);
			auto playerKindString = activePlayerContext.follow(&PlayerContext::KindID).follow(&CharacterKindID::KindString);
			auto playerHitPoint = activePlayerContext.follow(&PlayerContext::HitPoint);
			auto playerHitPointData = playerHitPoint.follow(&HitPoint::HitPointData);
			localGameData.Data.Player.ID = playerIDGUID;
			localGameData.Data.Player.KindID = playerKindString->GetString();
			localGameData.Data.Player.HP.CurrentHP = playerHitPointData.read(&CharacterHitPointData::_CurrentHP);
			localGameData.Data.Player.HP.MaximumHP = playerHitPointData.read(&CharacterHitPointData::_CurrentMaxHP);
			localGameData.Data.Player.HP.IsSetup = playerHitPointData.read(&CharacterHitPointData::_IsSetuped);
			localGameData.Data.Player.Position = activePlayerContext.read(&PlayerContext::PositionFast);

			// Enemy HP
			auto enemyContextManagedList = characterManager.follow(&CharacterManager::EnemyContextList);
			localGameData.AllEnemiesBacking = std::span(enemyContextManagedList->begin(), enemyContextManagedList->end()) |
			                                  std::views::transform([&localGameData](const EnemyContext *enemyContext)
			                                                        {
				                                              auto protectedEnemyContext = protect(enemyContext);
															  auto idGUID = protectedEnemyContext.follow(&EnemyContext::ID).read(&ContextID::_RawID);
															  auto kindString = protectedEnemyContext.follow(&EnemyContext::KindID).follow(&CharacterKindID::KindString);
				                                              auto hitPointData = protectedEnemyContext.follow(&EnemyContext::HitPoint).follow(&HitPoint::HitPointData);
															  auto enemyFormationMemberInfo = protectedEnemyContext.follow(&EnemyContext::FormationMemberInfo);
															  auto enemyPosition = enemyFormationMemberInfo.read(&EnemyFormationMemberInfo::Position);
															  auto enemySpawned = static_cast<bool>(enemyFormationMemberInfo);
				                                              return EnemyData
				                                              {
																.ID = idGUID,
				                                              	.KindID = kindString->GetString(),
				                                              	.HP = HPData
				                                              	{
				                                              		.CurrentHP = hitPointData.read(&CharacterHitPointData::_CurrentHP),
				                                              		.MaximumHP = hitPointData.read(&CharacterHitPointData::_CurrentMaxHP),
				                                              		.IsSetup = hitPointData.read(&CharacterHitPointData::_IsSetuped) != 0
				                                              	},
				                                              	.Position = enemyPosition,
																.Distance = enemySpawned ? localGameData.Data.Player.Position.EuclideanDistance(enemyPosition) : FLT_MAX,
																.IsSpawned = enemySpawned
				                                              }; }) |
			                                  std::ranges::to<std::vector>();

			localGameData.Data.AllEnemies = InteropArray{
			    .Size = localGameData.AllEnemiesBacking.size(),
			    .Values = localGameData.AllEnemiesBacking.data()};

			localGameData.FilteredEnemiesBacking = localGameData.AllEnemiesBacking |
			                                       std::views::filter([](const EnemyData &enemyData)
			                                                          { return enemyData.HP.CurrentHP != 0 && ((g_SRTSettings.DebugEnable && g_SRTSettings.DebugEnemiesShowNotSpawned) || (enemyData.IsSpawned && enemyData.Distance <= g_SRTSettings.EnemiesMaxDistance)); }) |
			                                       std::ranges::to<std::vector>();

			// constexpr auto compare = OrderBy([](const EnemyData &enemyData)
			//                                  { return enemyData.Distance; })
			//                              .ThenByDescending([](const EnemyData &enemyData)
			//                                                { return enemyData.HP.CurrentHP < enemyData.HP.MaximumHP; })
			//                              .ThenByDescending([](const EnemyData &enemyData)
			//                                                { return enemyData.HP.MaximumHP; });
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

			localGameData.HasData = true;

			// Release this index back to the data buffer.
			g_GameDataBufferReadIndex.store(writeIndex, std::memory_order_release);

			// Sleep until next read operation.
			Sleep(memoryReadIntervalInMS);
		}

		// Unhook version-specific hooks.
		if (getFOVOffsetPtr)
			oGetFOV.reset();
		oLockScene.reset();
		oUpdateBehavior.reset();

		logger->LogMessage("Hook::MainLoop() Loop exiting due to g_shutdownRequested state change!\n");
	}
}
