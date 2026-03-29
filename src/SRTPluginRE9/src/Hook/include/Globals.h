#ifndef SRTPLUGINRE9_GLOBALS_H
#define SRTPLUGINRE9_GLOBALS_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "DeferredWndProc.h"
#include "GameObjects.h"
#include "Logger.h"
#include "Settings.h"
#include <assert.h>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>
#include <uchar.h>
#include <windows.h>

namespace SRTPluginRE9
{
	constinit const char ToolNameShort[] = u8"SRT";
	constinit const char ToolName[] = u8"Speed Run Tool";

	constinit const char GameNameShort[] = u8"RE9";
	constinit const char GameName[] = u8"Resident Evil 9: Requiem (2026)";
}

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

extern "C"
{
	struct InteropArray
	{
		size_t Size;
		void *Values;
	};

	struct PositionalDataFloat
	{
		float_t X;
		float_t Y;
		float_t Z;
		// Store quaternion data also?

		PositionalDataFloat() {}
		PositionalDataFloat(const float_t &x, const float_t &y, const float_t &z) : X(x), Y(y), Z(z) {}
		PositionalDataFloat(const Vec3 &rhs) : X(rhs.x), Y(rhs.y), Z(rhs.z) {}

		PositionalDataFloat operator+(const PositionalDataFloat &rhs) const
		{
			return {X + rhs.X, Y + rhs.Y, Z + rhs.Z};
		}

		PositionalDataFloat operator-(const PositionalDataFloat &rhs) const
		{
			return {X - rhs.X, Y - rhs.Y, Z - rhs.Z};
		}

		const float_t EuclideanDistance(const PositionalDataFloat &rhs) const
		{
			auto [dx, dy, dz] = *this - rhs;
			return std::hypot(dx, dy, dz);
		}

		const float_t EuclideanDistance(const PositionalDataFloat &lhs, const PositionalDataFloat &rhs) const
		{
			auto [dx, dy, dz] = lhs - rhs;
			return std::hypot(dx, dy, dz);
		}
	};

	struct HPData
	{
		int32_t CurrentHP;
		int32_t MaximumHP;
		bool IsSetup;
	};

	struct EnemyData
	{
		std::string KindID;
		HPData HP{};
		PositionalDataFloat Position{};
		float_t Distance;
		bool IsSpawned = false;
	};

	struct PlayerData
	{
		std::string KindID;
		HPData HP{};
		PositionalDataFloat Position{};
	};

	struct SRTGameData
	{
		InteropArray InGameTimers; // [13]
		uint32_t RunningTimers;    // Enum
		int32_t DARank;
		int32_t DAScore;
		PlayerData Player{};
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
extern std::atomic<bool> g_shutdownRequested;
extern std::mutex g_LogMutex;
extern DeferredWndProc g_DeferredWndProc;
extern SRTSettings g_SRTSettings;

#if defined(DEBUG) || defined(_DEBUG)
#ifdef IM_ASSERT
#define SRT_ASSERTDEBUG(_EXPR) (IM_ASSERT(_EXPR))
#elifdef assert
#define SRT_ASSERTDEBUG(_EXPR) (assert(_EXPR))
#else
#define SRT_ASSERTDEBUG(_EXPR) ((void)(_EXPR))
#endif
#else
#define SRT_ASSERTDEBUG(_EXPR) ((void)(_EXPR))
#endif

#ifdef IM_ASSERT
#define SRT_ASSERT(_EXPR) (IM_ASSERT(_EXPR))
#elifdef assert
#define SRT_ASSERT(_EXPR) (assert(_EXPR))
#endif

#endif
