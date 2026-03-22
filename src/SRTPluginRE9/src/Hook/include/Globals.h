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

#define MAX_BACK_BUFFER_COUNT 3
struct DX12HookState
{
	// Device objects we create for our overlay
	Microsoft::WRL::ComPtr<ID3D12Device> device;
	ID3D12CommandQueue *commandQueue = nullptr;
	SRTPluginRE9::Hook::DescriptorHeaps heaps;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList;
	Microsoft::WRL::ComPtr<ID3D12Fence> fence;
	HANDLE fenceEvent = nullptr;
	uint64_t fenceValue = 0; // @TODO, Refactor: This should be named lastSignaledFenceValue

	FrameContext frameContexts[MAX_BACK_BUFFER_COUNT];
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

static bool SRT_Assert__(const char *Str, int *Ignore, ...);
static bool (*assertion_handler)(const char *Str, int *Ignore, ...) = SRT_Assert__;

#define ArrayCount(Array) (sizeof Array / sizeof *Array)

#ifndef Stringize
#define Stringize_(str) #str
#define Stringize(str) Stringize_(str)
#endif

#define SRT_Assert(cond_expr, ...) SRT_Assert_(cond_expr, ##__VA_ARGS__ )
#define SRT_Assert_(cond_expr, ...) do {    \
	static int ignore##__LINE__;        \
	if ((ignore##__LINE__)==0) {        \
		(!!(cond_expr) ||               \
		 assertion_handler("Assertion failed at " Stringize(__FILE__) ":" Stringize(__LINE__)": \""#cond_expr"\"\n", &ignore##__LINE__, __VA_ARGS__) \
		 && (__debugbreak(), 0));       \
	} } while (0)

#ifdef __clang__
#define String_Preamble (const String)
#define String_View_Preamble (const String_View)
#else
#define String_Preamble String
#define String_View_Preamble String_View
#endif

struct String { size_t Count; char* Data; };
inline size_t StringLength(const char*);

#define NullLiteral 0, (char*)""
#define NullString String { NullLiteral }
#define StringLiteral(literal) String_Preamble{ ArrayCount(literal)-1, (char*)(literal) }

inline size_t StringLength(const char* String) {
	size_t Count = 0;
	while (*String++) { ++Count; }
	return Count;
}

// @Note: this function sends output directly to windbg/visual studio 
//  debug output via an undocumented exception
static inline void __stdcall OutputToDebugger(const char* OutputString) {
	OutputDebugStringA(OutputString);
#if 0
	if (!IsDebuggerPresent()) return;
	ULONG_PTR Args[2];
	Args[0] = (ULONG_PTR)OutputString.Data;
	Args[1] = (ULONG_PTR)OutputString.Count;
	RaiseException(0x40010006, 0, 2, Args);
#endif
}

#pragma comment(lib, "user32")
static String AssertDialogText = StringLiteral("(Abort = crash, Retry = debug, Ignore = ignore)\n");
static inline bool SRT_Assert__(const char *String, int *Ignore, ...) {
	va_list ArgList;
	va_start(ArgList, Ignore);

	// @NOTE(j): 
	// This code is really bad and deserves to be refactored. You could 
	// absolutely consider this defacto UB but it generally tries to avoid reading
	// an invalid pointer outside of user-space. 
	//
	// I'm calling va_arg and hoping it doesn't hand us garbage from the void
	// like random data on the stack/register junk. Definitely cursed. Bad bad bad!!
	bool IsFirstArgPointerValid = false;
	const char *AssertUserStr = 0;
	{
		va_list ArgListCopy;
		va_copy(ArgListCopy, ArgList);
		AssertUserStr = va_arg(ArgListCopy, const char*);
		unsigned long B;
#ifdef __clang__
		unsigned __int64 A = __builtin_clzll((unsigned long long)AssertUserStr);
#else
		unsigned __int64 A = __lzcnt64((unsigned long long)AssertUserStr);
#endif
		unsigned char IsNonZero = _BitScanReverse64(&B, (unsigned long long)AssertUserStr);
		IsFirstArgPointerValid |= IsNonZero && (A == 17 && B == 46); // printf("%lu,%lu, IsNonZero = %u\n", A, B, IsNonZero);
		va_end(ArgListCopy);
	}

	DWORD Written = 0;
	uint32_t BufferLength = 0;
	char Buffer[2048] = {'\0'};
	HANDLE Stdout = GetStdHandle(STD_OUTPUT_HANDLE);
#define BufferAt Buffer + BufferLength
#define BufferLengthAt ArrayCount(Buffer) - BufferLength

	{ // Handle the condition part of the string
		const char* Full = String;
		const char* ConditionStart = 0;
		for (const char* At = Full; *At; ++At)
			if (At[0] == ':' && At[1] == ' ') ConditionStart = At + 2;
		if (!ConditionStart) ConditionStart = Full;

		int AddLength = (int)(ConditionStart - Full);
		BufferLength += _snprintf_s(BufferAt, BufferLengthAt, AddLength+1, "%.*s\n", AddLength, Full);

#define ConditionPreamble "Condition:\t"
		AddLength = int(strlen(ConditionPreamble)) + int(strlen(ConditionStart));
		BufferLength += _snprintf_s(BufferAt, BufferLengthAt, AddLength, ConditionPreamble"%s", ConditionStart);
		if (Stdout != INVALID_HANDLE_VALUE) WriteFile(Stdout, Buffer, BufferLength, &Written, 0);
	}

	// Handle the message if caller provided a valid C String as the first varadic parameter
	if (IsFirstArgPointerValid && strlen(AssertUserStr) > 1) {
		BufferLength += _vsnprintf_s(BufferAt, BufferLengthAt, BufferLengthAt, "Message:\t\'%s\'\n", ArgList);
		if (Stdout != INVALID_HANDLE_VALUE) WriteFile(Stdout, Buffer + Written, BufferLength - Written, &Written, 0);
	}

	va_end(ArgList);

	OutputToDebugger(/*{BufferLength,*/ Buffer);

	// TODO: This doesn't output to the logger sink that SRTPluginRE9 uses!
	BufferLength += _snprintf_s(BufferAt, BufferLengthAt, BufferLengthAt, 
			"\n%*s", (int)AssertDialogText.Count, AssertDialogText.Data);
	int r = MessageBoxA(0, Buffer, "SRTPluginRE9 - Assertion Failed", 0x22112);
	if (r == 3)
//#if !defined(DEBUG)
//        *(volatile int*)0 = 0;
//#else
		r = 4;
//#endif

	if (r==5) if (Ignore) *Ignore+=1;
	return (r==4) ? r : 0;
}

struct assert_entry {
	const char* FileName;
	const int Line;
	const int* IgnorePtr;
	int HitCount;
	struct assert_entry *Entry;
};

static struct assert_entry NilAssert = {};
static struct assert_entry* AssertIgnorableList = {&NilAssert};

// TODO: IgnoreOnce + IgnoreAlways like logic can be implemented here in the future if desired
static bool AreAssertsIgnoredForThisCode(const char* FileName, const int Line, int* IgnorePtr) {
	bool Result = 0;
	(void)(IgnorePtr);
	(void)(FileName);
	(void)(Line);
	return (Result);
}

// TODO: Requires cleanup pass
#if defined(DEBUG) || defined(_DEBUG)
#ifdef IM_ASSERT
#define SRT_ASSERTDEBUG(_EXPR) (IM_ASSERT(_EXPR))
#elifdef SRT_Assert
#define SRT_ASSERTDEBUG(_EXPR) (SRT_Assert(_EXPR))
#else
#define SRT_ASSERTDEBUG(_EXPR) ((void)(_EXPR))
#endif
#else
#define SRT_ASSERTDEBUG(_EXPR) ((void)(_EXPR))
#endif

#endif
