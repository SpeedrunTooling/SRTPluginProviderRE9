#ifndef SRTPLUGINRE9_DX12HOOK_H
#define SRTPLUGINRE9_DX12HOOK_H

#include "DescriptorHeapAllocator.h"
#include <d3d12.h>
#include <dxgi.h>
#include <dxgi1_4.h>
#include <expected>
#include <string>
#include <vector>
#include <wrl/client.h>

namespace SRTPluginRE9::DX12Hook
{
	struct FrameContext
	{
		Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator;
		Microsoft::WRL::ComPtr<ID3D12Resource> renderTarget;
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle{};
		uint64_t fenceValue = 0;
	};

	struct HookState
	{
		// Device objects we create for our overlay
		ID3D12Device *device = nullptr;
		ID3D12CommandQueue *commandQueue = nullptr;
		DX12DescriptorHeaps heaps;
		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList;
		Microsoft::WRL::ComPtr<ID3D12Fence> fence;
		HANDLE fenceEvent = nullptr;
		uint64_t fenceValue = 0;

		std::vector<FrameContext> frameContexts;
		UINT bufferCount = 0;
		HWND gameWindow = nullptr;
		WNDPROC origWndProc = nullptr;
		bool initialized = false;

		// Logo texture
		DX12DescriptorHandle logoHandle;
		ID3D12Resource *logoTexture = nullptr;
		int32_t logoWidth = 0;
		int32_t logoHeight = 0;
	};

	/// @brief A hook class wrapping DX12 resources.
	class DX12Hook
	{
	private:
		DX12Hook(); // Resolve and store DX12 VTables.

		// Function pointer definitions.
		using PFN_Present = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain3 *, UINT, UINT);
		using PFN_ResizeBuffers = HRESULT(STDMETHODCALLTYPE *)(IDXGISwapChain *, UINT, UINT, UINT, DXGI_FORMAT, UINT);
		using PFN_ExecuteCommandLists = void(STDMETHODCALLTYPE *)(ID3D12CommandQueue *, UINT, ID3D12CommandList *const *);

		// Original function pointers (set by MinHook)
		PFN_Present oPresent = nullptr;
		PFN_ResizeBuffers oResizeBuffers = nullptr;
		PFN_ExecuteCommandLists oExecuteCommandLists = nullptr;

		struct DX12VTableAddresses
		{
			void *present;
			void *resizeBuffers;
			void *executeCommandLists;
		};

		DX12VTableAddresses vtableAddresses{};
		HookState hookState{};

	public:
		// Deleted methods (only allow default ctor/dtor, move ctor, and move assignment)
		// DX12Hook(void) = delete;                        // default ctor (1)
		// ~DX12Hook(void) = delete;                       // default dtor (2)
		DX12Hook(const DX12Hook &) = delete; // copy ctor (3)
		// DX12Hook(const DX12Hook &&) = delete;            // move ctor (4)
		DX12Hook &operator=(const DX12Hook &) = delete; // copy assignment (5)
		                                                // DX12Hook &operator=(const DX12Hook &&) = delete; // move assignment (6)

		/// @brief Get the singleton instance of this class.
		/// @return The singleton instance of this class.
		static DX12Hook &GetInstance();

		/// @brief Gets the current hook state structure.
		/// @return The current hook state structure.
		const HookState &GetHookState();

		/// @brief Create resources.
		/// @param 1 The DXGI SwapChain v3 pointer.
		/// @param 2 The WndProc function pointer to hook calls into.
		/// @return The last HRESULT operation.
		HRESULT STDMETHODCALLTYPE Initialize(IDXGISwapChain3 *, LONG_PTR);

		/// @brief Release resources.
		/// @return The last HRESULT operation.
		HRESULT STDMETHODCALLTYPE Release();
	};
}

#endif
