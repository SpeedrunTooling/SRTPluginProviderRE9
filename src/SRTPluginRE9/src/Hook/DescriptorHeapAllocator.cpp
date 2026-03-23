#include "DescriptorHeapAllocator.h"

#include <format>

namespace SRTPluginRE9::DX12Hook
{
	auto DX12DescriptorHeap::Init(
	    ID3D12Device *device,
	    D3D12_DESCRIPTOR_HEAP_TYPE type,
	    uint32_t totalCapacity,
	    bool shaderVisible) -> std::expected<void, std::string>
	{
		if (!device)
			return std::unexpected("DescriptorHeap::Init - device is null");
		if (totalCapacity == 0)
			return std::unexpected("DescriptorHeap::Init - totalCapacity is 0");

		D3D12_DESCRIPTOR_HEAP_DESC desc{
		    .Type = type,
		    .NumDescriptors = totalCapacity,
		    .Flags = shaderVisible
		                 ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
		                 : D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
		    .NodeMask = 0,
		};

		HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap));
		if (FAILED(hr))
			return std::unexpected(
			    std::format("DescriptorHeap::Init - CreateDescriptorHeap failed: {:#x}", static_cast<uint32_t>(hr)));

		this->capacity = totalCapacity;
		this->descriptorSize = device->GetDescriptorHandleIncrementSize(type);
		this->cpuStart = heap->GetCPUDescriptorHandleForHeapStart();
		this->gpuStart = shaderVisible
		                     ? heap->GetGPUDescriptorHandleForHeapStart()
		                     : D3D12_GPU_DESCRIPTOR_HANDLE{0};
		this->nextIndex = 0;
		this->allocatedCount = 0;
		this->freeList.clear();

		return {};
	}

	DX12DescriptorHandle DX12DescriptorHeap::Allocate() noexcept
	{
		uint32_t index = UINT32_MAX;

		if (!freeList.empty())
		{
			index = freeList.back();
			freeList.pop_back();
		}
		else if (nextIndex < capacity)
		{
			index = nextIndex++;
		}
		else
		{
			return {};
		}

		++allocatedCount;

		DX12DescriptorHandle handle;
		handle.index = index;
		handle.cpu.ptr = cpuStart.ptr + static_cast<SIZE_T>(index) * descriptorSize;
		handle.gpu.ptr = gpuStart.ptr + static_cast<UINT64>(index) * descriptorSize;
		return handle;
	}

	void DX12DescriptorHeap::Free(DX12DescriptorHandle &handle) noexcept
	{
		if (!handle.IsValid() || handle.index >= capacity)
			return;

		freeList.push_back(handle.index);
		--allocatedCount;

		handle = {};
	}

	void DX12DescriptorHeap::Free(SIZE_T cpuPtr, UINT64 gpuPtr) noexcept
	{
		DX12DescriptorHandle handle{
		    .cpu = cpuPtr,
		    .gpu = gpuPtr,
		    .index = static_cast<uint32_t>(gpuPtr - gpuStart.ptr) / descriptorSize // Derive index from a start pointer, current pointer, and size.
		};

		if (!handle.IsValid() || handle.index >= capacity)
			return;

		freeList.push_back(handle.index);
		--allocatedCount;

		handle = {};
	}

	void DX12DescriptorHeap::Reset() noexcept
	{
		heap.Reset();
		cpuStart = {};
		gpuStart = {};
		descriptorSize = 0;
		capacity = 0;
		nextIndex = 0;
		allocatedCount = 0;
		freeList.clear();
	}

	auto DX12DescriptorHeaps::Init(
	    ID3D12Device *device,
	    uint32_t rtvCapacity,
	    uint32_t srvCapacity) -> std::expected<void, std::string>
	{
		auto rtvResult = rtv.Init(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, rtvCapacity, false);
		if (!rtvResult)
			return std::unexpected("DescriptorHeaps - RTV: " + rtvResult.error());

		auto srvResult = srv.Init(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, srvCapacity, true);
		if (!srvResult)
			return std::unexpected("DescriptorHeaps - SRV: " + srvResult.error());

		return {};
	}

	void DX12DescriptorHeaps::Reset() noexcept
	{
		rtv.Reset();
		srv.Reset();
	}
}
