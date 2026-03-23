#ifndef SRTPLUGINRE9_DESCRIPTORHEAPALLOCATOR_H
#define SRTPLUGINRE9_DESCRIPTORHEAPALLOCATOR_H

#include <cstdint>
#include <d3d12.h>
#include <expected>
#include <string>
#include <vector>
#include <wrl/client.h>

namespace SRTPluginRE9::DX12Hook
{
	struct DX12DescriptorHandle
	{
		D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
		D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
		uint32_t index = UINT32_MAX; // UINT32_MAX = invalid

		[[nodiscard]] bool IsValid() const noexcept { return index != UINT32_MAX; }
	};

	class DX12DescriptorHeap
	{
	public:
		DX12DescriptorHeap() = default;
		DX12DescriptorHeap(const DX12DescriptorHeap &) = delete;
		DX12DescriptorHeap &operator=(const DX12DescriptorHeap &) = delete;
		DX12DescriptorHeap(DX12DescriptorHeap &&) = default;
		DX12DescriptorHeap &operator=(DX12DescriptorHeap &&) = default;

		[[nodiscard]] auto Init(
		    ID3D12Device *device,
		    D3D12_DESCRIPTOR_HEAP_TYPE type,
		    uint32_t totalCapacity,
		    bool shaderVisible) -> std::expected<void, std::string>;

		[[nodiscard]] DX12DescriptorHandle Allocate() noexcept;

		void Free(DX12DescriptorHandle &handle) noexcept;
		void Free(SIZE_T cpuPtr, UINT64 gpuPtr) noexcept;

		[[nodiscard]] ID3D12DescriptorHeap *GetHeap() const noexcept { return heap.Get(); }

		[[nodiscard]] uint32_t GetAllocatedCount() const noexcept { return allocatedCount; }

		[[nodiscard]] uint32_t GetCapacity() const noexcept { return capacity; }

		void Reset() noexcept;

	private:
		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> heap;
		D3D12_CPU_DESCRIPTOR_HANDLE cpuStart{};
		D3D12_GPU_DESCRIPTOR_HANDLE gpuStart{};
		uint32_t descriptorSize = 0;
		uint32_t capacity = 0;
		uint32_t nextIndex = 0;
		uint32_t allocatedCount = 0;
		std::vector<uint32_t> freeList;
	};

	struct DX12DescriptorHeaps
	{
		DX12DescriptorHeap rtv;
		DX12DescriptorHeap srv;

		[[nodiscard]] auto Init(
		    ID3D12Device *device,
		    uint32_t rtvCapacity,
		    uint32_t srvCapacity) -> std::expected<void, std::string>;

		void Reset() noexcept;
	};
}

#endif
