#ifndef SRTPLUGINRE9_GLOBALS_H
#define SRTPLUGINRE9_GLOBALS_H

#include "DeferredWndProc.h"
#include "DescriptorHeapAllocator.h"
#include "Logger.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <optional>
#include <windows.h>

struct FrameContext
{
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator;
	Microsoft::WRL::ComPtr<ID3D12Resource> renderTarget;
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle{};
	uint64_t fenceValue = 0;
};

struct DX12HookState
{
	// Device objects we create for our overlay
	Microsoft::WRL::ComPtr<ID3D12Device> device;
	ID3D12CommandQueue *commandQueue = nullptr;
	SRTPluginRE9::Hook::DescriptorHeaps heaps;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList;
	Microsoft::WRL::ComPtr<ID3D12Fence> fence;
	HANDLE fenceEvent = nullptr;
	uint64_t fenceValue = 0;

	std::vector<FrameContext> frameContexts;
	UINT bufferCount = 0;
	HWND gameWindow = nullptr;
	WNDPROC origWndProc = nullptr;
	bool initialized = false;
};

extern HMODULE g_dllModule;
extern HANDLE g_mainThread;
extern FILE *g_logFile;
extern SRTPluginRE9::Logger::Logger *logger;
extern SRTPluginRE9::Logger::LoggerUIData *g_LoggerUIData;
extern std::optional<std::uintptr_t> g_BaseAddress;
extern DX12HookState g_dx12HookState;
extern std::atomic<bool> g_shutdownRequested;
extern std::mutex g_LogMutex;
extern DeferredWndProc g_DeferredWndProc;

#endif
