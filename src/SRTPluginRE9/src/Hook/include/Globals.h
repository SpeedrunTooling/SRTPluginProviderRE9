#ifndef SRTPLUGINRE9_GLOBALS_H
#define SRTPLUGINRE9_GLOBALS_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "DeferredWndProc.h"
#include "DescriptorHeapAllocator.h"
#include "Logger.h"
#include "Settings.h"
#include <assert.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <windows.h>

namespace SRTPluginRE9::Version
{
// Major version number. This is defined at compile time so this is just a placeholder.
#ifndef APP_VERSION_MAJOR
#define APP_VERSION_MAJOR 0
#endif
	constinit const uint8_t Major = APP_VERSION_MAJOR;

// Minor version number. This is defined at compile time so this is just a placeholder.
#ifndef APP_VERSION_MINOR
#define APP_VERSION_MINOR 1
#endif
	constinit const uint8_t Minor = APP_VERSION_MINOR;

// Patch version number. This is defined at compile time so this is just a placeholder.
#ifndef APP_VERSION_PATCH
#define APP_VERSION_PATCH 0
#endif
	constinit const uint8_t Patch = APP_VERSION_PATCH;

// Build number. This is defined at compile time so this is just a placeholder.
#ifndef APP_VERSION_BUILD
#define APP_VERSION_BUILD 1
#endif
	constinit const uint8_t Build = APP_VERSION_BUILD;

// Pre-release tag. This is defined at compile time so this is just a placeholder.
#ifndef APP_VERSION_PRERELEASE_TAG
#define APP_VERSION_PRERELEASE_TAG ""
#endif
	constinit const std::string_view PreReleaseTag = APP_VERSION_PRERELEASE_TAG;

// Build SHA hash. This is defined at compile time so this is just a placeholder.
#ifndef APP_VERSION_BUILD_HASH
#define APP_VERSION_BUILD_HASH ""
#endif
	constinit const std::string_view BuildHash = APP_VERSION_BUILD_HASH;

// Semantic Versioning string. This is defined at compile time so this is just a placeholder.
#ifndef APP_VERSION_SEMVER
#define APP_VERSION_SEMVER "0.1.0+1"
#endif
	constinit const std::string_view SemVer = APP_VERSION_SEMVER;
}

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

extern "C"
{
	struct InteropArray
	{
		size_t Size;
		void *Values;
	};

	struct PositionalData
	{
		float_t X;
		float_t Y;
		float_t Z;
		// Store quaternion data also?
	};

	struct HPData
	{
		int32_t CurrentHP;
		int32_t MaximumHP;
		bool IsSetup;
	};

	struct EnemyData
	{
		uint16_t KindID;
		HPData HP;
		PositionalData Position;
	};

	struct SRTGameData
	{
		InteropArray InGameTimers; // [13]
		uint32_t RunningTimers;    // Enum
		int32_t DARank;
		int32_t DAScore;
		HPData PlayerHP;
		InteropArray AllEnemies;
		InteropArray FilteredEnemies;
	};
}

// Double-buffered to allow the main thread and UI thread to operate on data independently.
struct GameDataBuffer
{
	SRTGameData Data{};

	// Backing storage - InteropArray::Values will point into these.
	std::vector<EnemyData> AllEnemiesBacking{};
	std::vector<EnemyData> FilteredEnemiesBacking{};
	std::vector<uint64_t> TimersBacking{};
};

inline GameDataBuffer g_GameDataBuffers[2]{};
inline std::atomic<uint32_t> g_GameDataBufferReadIndex{0};

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
extern SRTSettings g_SRTSettings;

#if defined(DEBUG) || defined(_DEBUG)
#ifdef IM_ASSERT
#define SRT_ASSERTDEBUG(_EXPR) IM_ASSERT(_EXPR)
#elifdef assert
#define SRT_ASSERTDEBUG(_EXPR) assert(_EXPR)
#else
#define SRT_ASSERTDEBUG(_EXPR) ((void)(_EXPR))
#endif
#else
#define SRT_ASSERTDEBUG(_EXPR) ((void)(_EXPR))
#endif

#ifdef IM_ASSERT
#define SRT_ASSERT(_EXPR) IM_ASSERT(_EXPR)
#elifdef assert
#define SRT_ASSERT(_EXPR) assert(_EXPR)
#endif

#endif
