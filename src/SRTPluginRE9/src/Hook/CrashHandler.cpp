#include "CrashHandler.h"

#include "Globals.h"
#include "Logger.h"

#include <DbgHelp.h>
#include <TlHelp32.h>
#include <psapi.h>

// Everything below the Install() boundary runs on a thread whose heap and stack may already
// be corrupt. That rules out the CRT, std::format, std::string, and any lock — including the
// logger's own mutex, which the crashing thread may already hold. All formatting is done with
// hand-rolled appenders into static buffers, and every risky call is wrapped in SEH.
//
// Ordering inside the filter is deliberate: the minidump is written FIRST, because it is the
// artifact we cannot regenerate. The richer text report is written afterwards, so if its
// (more adventurous) thread enumeration faults, the dump has already landed on disk.
//
// Note that functions containing __try must not hold any object with a destructor (C2712),
// which is another reason everything here is raw buffers.

namespace SRTPluginRE9::Hook::CrashHandler
{
	namespace
	{
		constexpr size_t ReportBufferSize = 512 * 1024;
		constexpr size_t MaxExtraMemoryRanges = 64;
		constexpr size_t MaxModules = 512;
		constexpr size_t MaxStackFrames = 64;
		constexpr ULONG StackGuaranteeBytes = 64 * 1024;

		// The process is dying either way; a bounded wait is strictly better than hanging the
		// game forever if dbghelp deadlocks on a lock the faulting thread already holds.
		// Generous enough for a several-hundred-megabyte dump on a slow disk.
		constexpr DWORD CrashWorkTimeoutMs = 60 * 1000;

		// CommentStreamA is a reserved stream type that dbghelp explicitly permits as a user
		// stream. WinDbg surfaces it automatically when the dump is loaded.
		constexpr ULONG32 CommentStreamAType = 10;

		BuildIdentity g_selfIdentity{};
		BuildIdentity g_exeIdentity{};
		std::atomic_flag g_inHandler = ATOMIC_FLAG_INIT;

		char g_reportBuffer[ReportBufferSize]{};
		char g_commentBuffer[SRTPluginRE9::Logger::LogRingCapacity + 1]{};

		// One pass over the loader's module list, taken once per crash. Every later address
		// lookup reads this array instead of calling GetModuleHandleExW, which would take the
		// loader lock again for every stack frame and hook target.
		struct ModuleEntry
		{
			uintptr_t Base;
			uint32_t Size;
			uint32_t LinkTimestamp;
			GUID PdbGuid;
			uint32_t PdbAge;
			bool HasPdbId;
			wchar_t Path[MAX_PATH];
		};

		ModuleEntry g_moduleSnapshot[MaxModules]{};
		uint32_t g_moduleSnapshotCount = 0;

		// Everything the filter hands to the worker thread. Plain data in static storage so
		// nothing has to be allocated at crash time.
		struct CrashJob
		{
			EXCEPTION_POINTERS *ExceptionInfo;
			DWORD FaultingThreadId;
			wchar_t DumpPath[MAX_PATH];
			wchar_t ReportPath[MAX_PATH];
			bool DumpWritten;
			DWORD DumpError;
		};

		CrashJob g_job{};

		// The worker is created at install time, not at crash time: CreateThread takes the
		// loader lock, and the whole point of the worker is to survive a crash that happened
		// while the faulting thread held it.
		HANDLE g_workerThread = nullptr;
		HANDLE g_workerStart = nullptr;
		HANDLE g_workerDone = nullptr;

		// Report file stays open across stages so each stage can be flushed independently.
		HANDLE g_reportFile = INVALID_HANDLE_VALUE;

		struct MemoryRange
		{
			ULONG64 Base;
			ULONG Size;
		};

		struct ExtraMemory
		{
			MemoryRange Ranges[MaxExtraMemoryRanges];
			ULONG Count;
			ULONG Next;
		};

		ExtraMemory g_extraMemory{};

		// ---------------------------------------------------------------------------------
		// Allocation-free text building.
		// ---------------------------------------------------------------------------------

		struct Appender
		{
			char *Cur;
			char *End;

			void Ch(char c) noexcept
			{
				if (Cur < End)
					*Cur++ = c;
			}

			void Str(const char *s) noexcept
			{
				if (s == nullptr)
					return;
				while (*s != '\0' && Cur < End)
					*Cur++ = *s++;
			}

			void View(std::string_view s) noexcept
			{
				for (const char c : s)
					Ch(c);
			}

			// Deliberately lossy: crash reports only ever carry paths and module names.
			void Wide(const wchar_t *s) noexcept
			{
				if (s == nullptr)
					return;
				while (*s != L'\0' && Cur < End)
				{
					const wchar_t wc = *s++;
					*Cur++ = (wc < 0x80) ? static_cast<char>(wc) : '?';
				}
			}

			void Hex(uint64_t value, int digits) noexcept
			{
				static constexpr char kDigits[] = "0123456789ABCDEF";
				if (digits <= 0)
				{
					digits = 1;
					for (uint64_t probe = value; probe >= 16; probe >>= 4)
						++digits;
				}
				for (int shift = (digits - 1) * 4; shift >= 0; shift -= 4)
					Ch(kDigits[(value >> shift) & 0xF]);
			}

			void Dec(uint64_t value) noexcept
			{
				char tmp[24];
				int i = 0;
				do
				{
					tmp[i++] = static_cast<char>('0' + (value % 10));
					value /= 10;
				} while (value != 0 && i < static_cast<int>(sizeof(tmp)));
				while (i > 0)
					Ch(tmp[--i]);
			}

			void Dec2(unsigned value) noexcept
			{
				Ch(static_cast<char>('0' + ((value / 10) % 10)));
				Ch(static_cast<char>('0' + (value % 10)));
			}

			[[nodiscard]] size_t Length(const char *start) const noexcept
			{
				return static_cast<size_t>(Cur - start);
			}

			[[nodiscard]] size_t Remaining() const noexcept
			{
				return static_cast<size_t>(End - Cur);
			}
		};

		// The bare 32-hex-digit form. Only useful immediately followed by the age, as the
		// symbol-store key — see AppendPdbIdentity.
		void AppendGuid(Appender &out, const GUID &guid) noexcept
		{
			out.Hex(guid.Data1, 8);
			out.Hex(guid.Data2, 4);
			out.Hex(guid.Data3, 4);
			for (const unsigned char byte : guid.Data4)
				out.Hex(byte, 2);
		}

		void AppendGuidBraced(Appender &out, const GUID &guid) noexcept
		{
			out.Ch('{');
			out.Hex(guid.Data1, 8);
			out.Ch('-');
			out.Hex(guid.Data2, 4);
			out.Ch('-');
			out.Hex(guid.Data3, 4);
			out.Ch('-');
			out.Hex(guid.Data4[0], 2);
			out.Hex(guid.Data4[1], 2);
			out.Ch('-');
			for (size_t i = 2; i < 8; ++i)
				out.Hex(guid.Data4[i], 2);
			out.Ch('}');
		}

		// Symbol servers key on the GUID and age concatenated with no separator, and the age is
		// in hex — so age 13 renders as a trailing 'D' that looks like part of the GUID. That
		// key is what a symbol-store path contains and what has to be pasted into tools, but it
		// is unreadable on its own, so emit the human form and the exact key side by side.
		void AppendPdbIdentity(Appender &out, const GUID &guid, uint32_t age) noexcept
		{
			out.Str("PDB GUID ");
			AppendGuidBraced(out, guid);
			out.Str(" Age ");
			out.Dec(age);
			out.Str(" (symbol key ");
			AppendGuid(out, guid);
			out.Hex(age, 0);
			out.Ch(')');
		}

		// ---------------------------------------------------------------------------------
		// Module / memory helpers.
		// ---------------------------------------------------------------------------------

		bool IsReadable(uintptr_t address, size_t size) noexcept
		{
			MEMORY_BASIC_INFORMATION mbi{};
			if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)) == 0)
				return false;
			if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD) != 0)
				return false;

			switch (mbi.Protect & 0xFF)
			{
				case PAGE_READONLY:
				case PAGE_READWRITE:
				case PAGE_WRITECOPY:
				case PAGE_EXECUTE_READ:
				case PAGE_EXECUTE_READWRITE:
				case PAGE_EXECUTE_WRITECOPY:
					break;
				default:
					return false;
			}

			const auto regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
			return address + size <= regionEnd;
		}

		const wchar_t *LeafName(const wchar_t *path) noexcept
		{
			const wchar_t *leaf = path;
			for (const wchar_t *scan = path; *scan != L'\0'; ++scan)
				if (*scan == L'\\' || *scan == L'/')
					leaf = scan + 1;
			return leaf;
		}

		bool EqualsIgnoreCaseW(const wchar_t *a, const wchar_t *b) noexcept
		{
			while (*a != L'\0' && *b != L'\0')
			{
				const wchar_t lowerA = (*a >= L'A' && *a <= L'Z') ? static_cast<wchar_t>(*a + 32) : *a;
				const wchar_t lowerB = (*b >= L'A' && *b <= L'Z') ? static_cast<wchar_t>(*b + 32) : *b;
				if (lowerA != lowerB)
					return false;
				++a;
				++b;
			}
			return *a == L'\0' && *b == L'\0';
		}

		bool ContainsIgnoreCaseW(const wchar_t *haystack, const wchar_t *needle) noexcept
		{
			for (const wchar_t *start = haystack; *start != L'\0'; ++start)
			{
				const wchar_t *a = start;
				const wchar_t *b = needle;
				while (*a != L'\0' && *b != L'\0')
				{
					const wchar_t lowerA = (*a >= L'A' && *a <= L'Z') ? static_cast<wchar_t>(*a + 32) : *a;
					if (lowerA != *b)
						break;
					++a;
					++b;
				}
				if (*b == L'\0')
					return true;
			}
			return false;
		}

		BuildIdentity ReadBuildIdentity(const uint8_t *base) noexcept;

		// Takes the single loader-lock pass. Sizes come from each module's PE header rather
		// than an API call, so the only locked operations are EnumProcessModules and one
		// GetModuleFileNameW per module.
		void TakeModuleSnapshot() noexcept
		{
			g_moduleSnapshotCount = 0;

			HMODULE modules[MaxModules];
			DWORD bytesNeeded = 0;
			if (EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &bytesNeeded) == FALSE)
				return;

			const size_t reported = bytesNeeded / sizeof(HMODULE);
			const size_t count = (reported < MaxModules) ? reported : MaxModules;

			for (size_t i = 0; i < count; ++i)
			{
				const BuildIdentity identity = ReadBuildIdentity(reinterpret_cast<const uint8_t *>(modules[i]));

				ModuleEntry &entry = g_moduleSnapshot[g_moduleSnapshotCount];
				entry.Base = reinterpret_cast<uintptr_t>(modules[i]);
				entry.Size = identity.SizeOfImage;
				entry.LinkTimestamp = identity.LinkTimestamp;
				entry.PdbGuid = identity.PdbGuid;
				entry.PdbAge = identity.PdbAge;
				entry.HasPdbId = identity.Valid;
				entry.Path[0] = L'\0';
				GetModuleFileNameW(modules[i], entry.Path, MAX_PATH);

				++g_moduleSnapshotCount;
			}
		}

		// Resolves an address to "module+RVA" purely from the snapshot — no API calls, so this
		// is safe to run against a corrupt heap or a held loader lock.
		const ModuleEntry *FindModule(uintptr_t address) noexcept
		{
			for (uint32_t i = 0; i < g_moduleSnapshotCount; ++i)
			{
				const ModuleEntry &entry = g_moduleSnapshot[i];
				if (entry.Size != 0 && address >= entry.Base && address < entry.Base + entry.Size)
					return &entry;
			}
			return nullptr;
		}

		void AppendResolvedAddress(Appender &out, uintptr_t address) noexcept
		{
			out.Str("0x");
			out.Hex(address, 16);

			const ModuleEntry *owner = FindModule(address);
			if (owner == nullptr)
			{
				// Either genuinely unmapped, or the snapshot never ran. Both are worth
				// distinguishing from a resolved address, and neither is worth a lock.
				out.Str(g_moduleSnapshotCount == 0 ? "  <no module snapshot>" : "  <not in any loaded module>");
				return;
			}

			out.Str("  ");
			out.Wide(LeafName(owner->Path));
			out.Str("+0x");
			out.Hex(address - owner->Base, 0);
		}

		BuildIdentity ReadBuildIdentity(const uint8_t *base) noexcept
		{
			BuildIdentity identity{};
			__try
			{
				const auto *dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
				if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE)
					return identity;

				const auto *ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dosHeader->e_lfanew);
				if (ntHeaders->Signature != IMAGE_NT_SIGNATURE)
					return identity;

				identity.ImageBase = reinterpret_cast<uintptr_t>(base);
				identity.SizeOfImage = ntHeaders->OptionalHeader.SizeOfImage;
				identity.LinkTimestamp = ntHeaders->FileHeader.TimeDateStamp;

				const auto &debugDir = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
				if (debugDir.VirtualAddress == 0 || debugDir.Size < sizeof(IMAGE_DEBUG_DIRECTORY))
					return identity;

				const auto *entries = reinterpret_cast<const IMAGE_DEBUG_DIRECTORY *>(base + debugDir.VirtualAddress);
				const size_t entryCount = debugDir.Size / sizeof(IMAGE_DEBUG_DIRECTORY);

				for (size_t i = 0; i < entryCount; ++i)
				{
					if (entries[i].Type != IMAGE_DEBUG_TYPE_CODEVIEW || entries[i].AddressOfRawData == 0)
						continue;

					// RSDS layout: 'SDSR' magic, GUID, ULONG age, NUL-terminated UTF-8 pdb path.
					const uint8_t *record = base + entries[i].AddressOfRawData;
					if (*reinterpret_cast<const uint32_t *>(record) != 0x53445352u)
						continue;

					identity.PdbGuid = *reinterpret_cast<const GUID *>(record + 4);
					identity.PdbAge = *reinterpret_cast<const uint32_t *>(record + 20);

					const auto *path = reinterpret_cast<const char *>(record + 24);
					size_t n = 0;
					while (n + 1 < sizeof(identity.PdbPath) && path[n] != '\0')
					{
						identity.PdbPath[n] = path[n];
						++n;
					}
					identity.PdbPath[n] = '\0';
					identity.Valid = true;
					break;
				}
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				identity.Valid = false;
			}
			return identity;
		}

		void AppendBuildIdentity(Appender &out, const char *label, const BuildIdentity &identity) noexcept
		{
			out.Str(label);
			if (!identity.Valid)
			{
				out.Str(" : <no CodeView record>\n");
				return;
			}
			out.Str("\n    PDB identity  : ");
			AppendPdbIdentity(out, identity.PdbGuid, identity.PdbAge);
			out.Str("\n    Link timestamp: 0x");
			out.Hex(identity.LinkTimestamp, 8);
			out.Str("\n    Image base    : 0x");
			out.Hex(identity.ImageBase, 16);
			out.Str("  size 0x");
			out.Hex(identity.SizeOfImage, 0);
			out.Str("\n    PDB path      : ");
			out.Str(identity.PdbPath);
			out.Ch('\n');
		}

		// ---------------------------------------------------------------------------------
		// Stack unwinding. Deliberately avoids SymInitialize/SymFromAddr — those allocate
		// heavily and load DLLs, which is exactly what we cannot do here. module+RVA plus a
		// matching PDB gives the analyst everything anyway.
		// ---------------------------------------------------------------------------------

		size_t UnwindStack(const CONTEXT &start, uintptr_t *frames, size_t maxFrames) noexcept
		{
			size_t count = 0;
			__try
			{
				CONTEXT ctx = start;
				while (count < maxFrames && ctx.Rip != 0)
				{
					frames[count++] = static_cast<uintptr_t>(ctx.Rip);

					DWORD64 imageBase = 0;
					PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(ctx.Rip, &imageBase, nullptr);
					if (function == nullptr)
					{
						// Leaf function with no unwind data: the return address sits at RSP.
						if (!IsReadable(static_cast<uintptr_t>(ctx.Rsp), sizeof(DWORD64)))
							break;
						ctx.Rip = *reinterpret_cast<DWORD64 *>(ctx.Rsp);
						ctx.Rsp += sizeof(DWORD64);
						continue;
					}

					PVOID handlerData = nullptr;
					DWORD64 establisherFrame = 0;
					const DWORD64 previousRsp = ctx.Rsp;
					RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, ctx.Rip, function, &ctx, &handlerData, &establisherFrame, nullptr);

					// Bail out rather than loop forever on unwind info that doesn't progress.
					if (ctx.Rsp <= previousRsp)
						break;
				}
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				// Keep whatever frames we already collected.
			}
			return count;
		}

		// ---------------------------------------------------------------------------------
		// Minidump callbacks.
		// ---------------------------------------------------------------------------------

		bool IsWhitelistedModule(const wchar_t *fullPath) noexcept
		{
			if (fullPath == nullptr)
				return false;

			const wchar_t *leaf = LeafName(fullPath);

			// re9.exe is the game, dinput8.dll is how REFramework injects itself, and we
			// obviously want our own code.
			if (EqualsIgnoreCaseW(leaf, L"re9.exe") ||
			    EqualsIgnoreCaseW(leaf, L"srtpluginre9.dll") ||
			    EqualsIgnoreCaseW(leaf, L"dinput8.dll"))
				return true;

			// Any REFramework native plugin, wherever the user installed the game.
			return ContainsIgnoreCaseW(fullPath, L"reframework");
		}

		void AddExtraRange(uintptr_t center, ULONG halfWindow) noexcept
		{
			if (g_extraMemory.Count >= MaxExtraMemoryRanges || center == 0)
				return;

			// Clamp to something actually mapped — a bogus range can fail the whole dump.
			MEMORY_BASIC_INFORMATION mbi{};
			if (VirtualQuery(reinterpret_cast<LPCVOID>(center), &mbi, sizeof(mbi)) == 0 || mbi.State != MEM_COMMIT)
				return;

			const auto regionStart = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
			const uintptr_t regionEnd = regionStart + mbi.RegionSize;

			uintptr_t begin = (center > halfWindow) ? center - halfWindow : 0;
			uintptr_t end = center + halfWindow;
			if (begin < regionStart)
				begin = regionStart;
			if (end > regionEnd)
				end = regionEnd;
			if (end <= begin)
				return;

			g_extraMemory.Ranges[g_extraMemory.Count].Base = begin;
			g_extraMemory.Ranges[g_extraMemory.Count].Size = static_cast<ULONG>(end - begin);
			++g_extraMemory.Count;
		}

		void BuildExtraMemoryRanges(const CONTEXT *context) noexcept
		{
			g_extraMemory.Count = 0;
			g_extraMemory.Next = 0;

			if (context == nullptr)
				return;

			// The faulting instruction and its neighbourhood — the single most valuable thing
			// to capture, and precisely what a MiniDumpNormal dump can never disassemble.
			AddExtraRange(static_cast<uintptr_t>(context->Rip), 0x1000);

			const DWORD64 registers[] = {
			    context->Rax, context->Rcx, context->Rdx, context->Rbx,
			    context->Rsp, context->Rbp, context->Rsi, context->Rdi,
			    context->R8, context->R9, context->R10, context->R11,
			    context->R12, context->R13, context->R14, context->R15};

			for (const DWORD64 value : registers)
				AddExtraRange(static_cast<uintptr_t>(value), 0x800);

			// The whole plugin image is only ~1.4 MB, so always take it — our own frames then
			// disassemble even when code segments are switched off.
			if (g_selfIdentity.ImageBase != 0 && g_selfIdentity.SizeOfImage != 0 &&
			    g_extraMemory.Count < MaxExtraMemoryRanges)
			{
				g_extraMemory.Ranges[g_extraMemory.Count].Base = g_selfIdentity.ImageBase;
				g_extraMemory.Ranges[g_extraMemory.Count].Size = g_selfIdentity.SizeOfImage;
				++g_extraMemory.Count;
			}
		}

		BOOL CALLBACK MiniDumpCallback(
		    PVOID /*callbackParam*/,
		    const PMINIDUMP_CALLBACK_INPUT callbackInput,
		    PMINIDUMP_CALLBACK_OUTPUT callbackOutput)
		{
			if (callbackInput == nullptr || callbackOutput == nullptr)
				return FALSE;

			switch (callbackInput->CallbackType)
			{
				case ModuleCallback:
				{
					// Keep ModuleWriteModule and ModuleWriteCvRecord for every module — the
					// CV record is what lets a debugger bind symbols at all. Only the bulky
					// code and data segments get stripped for modules we don't care about.
					if (!IsWhitelistedModule(callbackInput->Module.FullPath))
						callbackOutput->ModuleWriteFlags &= ~static_cast<ULONG>(ModuleWriteDataSeg | ModuleWriteCodeSegs);
					return TRUE;
				}

				case MemoryCallback:
				{
					if (g_extraMemory.Next < g_extraMemory.Count)
					{
						const MemoryRange &range = g_extraMemory.Ranges[g_extraMemory.Next++];
						callbackOutput->MemoryBase = range.Base;
						callbackOutput->MemorySize = range.Size;
					}
					else
					{
						callbackOutput->MemoryBase = 0;
						callbackOutput->MemorySize = 0;
					}
					return TRUE;
				}

				default:
					return TRUE;
			}
		}

		// ---------------------------------------------------------------------------------
		// Filenames and raw file IO.
		// ---------------------------------------------------------------------------------

		void AppendTwoDigitsW(wchar_t *&cursor, unsigned value) noexcept
		{
			*cursor++ = static_cast<wchar_t>(L'0' + ((value / 10) % 10));
			*cursor++ = static_cast<wchar_t>(L'0' + (value % 10));
		}

		// Builds "SRTPluginRE9_YYYYMMDD-HHMMSS.mmmUTC<suffix>" with no allocation.
		// Relative, so it lands in the game's working directory exactly as before.
		void BuildCrashFileName(const SYSTEMTIME &now, const wchar_t *suffix, wchar_t (&out)[MAX_PATH]) noexcept
		{
			static constexpr wchar_t kPrefix[] = L"SRTPluginRE9_";
			wchar_t *cursor = out;
			for (const wchar_t *p = kPrefix; *p != L'\0'; ++p)
				*cursor++ = *p;

			AppendTwoDigitsW(cursor, now.wYear / 100u);
			AppendTwoDigitsW(cursor, now.wYear % 100u);
			AppendTwoDigitsW(cursor, now.wMonth);
			AppendTwoDigitsW(cursor, now.wDay);
			*cursor++ = L'-';
			AppendTwoDigitsW(cursor, now.wHour);
			AppendTwoDigitsW(cursor, now.wMinute);
			AppendTwoDigitsW(cursor, now.wSecond);
			*cursor++ = L'.';
			*cursor++ = static_cast<wchar_t>(L'0' + ((now.wMilliseconds / 100u) % 10u));
			AppendTwoDigitsW(cursor, now.wMilliseconds % 100u);

			static constexpr wchar_t kUtc[] = L"UTC";
			for (const wchar_t *p = kUtc; *p != L'\0'; ++p)
				*cursor++ = *p;
			for (const wchar_t *p = suffix; *p != L'\0'; ++p)
				*cursor++ = *p;
			*cursor = L'\0';
		}

		// Appends to the existing log without touching the CRT FILE* the logger owns.
		// The logger opened it with SH_DENYNO, so a second append handle is permitted.
		void AppendToLogFile(const char *text, DWORD size) noexcept
		{
			const HANDLE file = CreateFileW(
			    L"SRTPluginRE9.log",
			    FILE_APPEND_DATA,
			    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			    nullptr,
			    OPEN_EXISTING,
			    FILE_ATTRIBUTE_NORMAL,
			    nullptr);
			if (file == INVALID_HANDLE_VALUE)
				return;

			SetFilePointer(file, 0, nullptr, FILE_END);
			DWORD written = 0;
			WriteFile(file, text, size, &written, nullptr);
			CloseHandle(file);
		}

		// ---------------------------------------------------------------------------------
		// Report sections.
		// ---------------------------------------------------------------------------------

		void AppendExceptionSummary(Appender &out, const EXCEPTION_RECORD *record) noexcept
		{
			out.Str("Exception code   : 0x");
			out.Hex(record->ExceptionCode, 8);
			switch (record->ExceptionCode)
			{
				case EXCEPTION_ACCESS_VIOLATION:
					out.Str("  (ACCESS_VIOLATION)");
					break;
				case EXCEPTION_STACK_OVERFLOW:
					out.Str("  (STACK_OVERFLOW)");
					break;
				case EXCEPTION_ILLEGAL_INSTRUCTION:
					out.Str("  (ILLEGAL_INSTRUCTION)");
					break;
				case EXCEPTION_INT_DIVIDE_BY_ZERO:
					out.Str("  (INT_DIVIDE_BY_ZERO)");
					break;
				case EXCEPTION_PRIV_INSTRUCTION:
					out.Str("  (PRIV_INSTRUCTION)");
					break;
				case EXCEPTION_IN_PAGE_ERROR:
					out.Str("  (IN_PAGE_ERROR)");
					break;
				default:
					break;
			}

			// Raw only — this stage runs before the module snapshot so that it stays free of
			// loader-lock calls. WriteCrashReport emits the resolved form once the snapshot
			// exists; if that never happens, the analyst still has the address to map by hand.
			out.Str("\nException address: 0x");
			out.Hex(reinterpret_cast<uintptr_t>(record->ExceptionAddress), 16);
			out.Ch('\n');

			if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2)
			{
				out.Str("Access violation : ");
				switch (record->ExceptionInformation[0])
				{
					case 0:
						out.Str("READ from 0x");
						break;
					case 1:
						out.Str("WRITE to 0x");
						break;
					case 8:
						out.Str("EXECUTE at 0x");
						break;
					default:
						out.Str("UNKNOWN at 0x");
						break;
				}
				out.Hex(record->ExceptionInformation[1], 16);
				out.Ch('\n');
			}

			out.Str("Parameters       :");
			for (DWORD i = 0; i < record->NumberParameters && i < EXCEPTION_MAXIMUM_PARAMETERS; ++i)
			{
				out.Str(" 0x");
				out.Hex(record->ExceptionInformation[i], 0);
			}
			out.Ch('\n');
		}

		void AppendRegisters(Appender &out, const CONTEXT *context) noexcept
		{
			struct NamedRegister
			{
				const char *Name;
				DWORD64 Value;
			};

			const NamedRegister registers[] = {
			    {"RAX", context->Rax}, {"RBX", context->Rbx}, {"RCX", context->Rcx}, {"RDX", context->Rdx}, {"RSI", context->Rsi}, {"RDI", context->Rdi}, {"RBP", context->Rbp}, {"RSP", context->Rsp}, {"R8 ", context->R8}, {"R9 ", context->R9}, {"R10", context->R10}, {"R11", context->R11}, {"R12", context->R12}, {"R13", context->R13}, {"R14", context->R14}, {"R15", context->R15}, {"RIP", context->Rip}};

			out.Str("\n--- Registers ---\n");
			for (size_t i = 0; i < sizeof(registers) / sizeof(registers[0]); ++i)
			{
				out.Str(registers[i].Name);
				out.Str(" = 0x");
				out.Hex(registers[i].Value, 16);
				out.Str((i % 2 == 1) ? "\n" : "   ");
			}
			out.Str("\nEFlags = 0x");
			out.Hex(context->EFlags, 8);
			out.Ch('\n');
		}

		void AppendFaultingStack(Appender &out, const CONTEXT *context, DWORD faultingThreadId) noexcept
		{
			// The TID is passed in rather than read from the current thread: this runs on the
			// crash worker, not on the thread that faulted.
			out.Str("\n--- Faulting thread stack (TID ");
			out.Dec(faultingThreadId);
			out.Str(") ---\n");

			uintptr_t frames[MaxStackFrames]{};
			const size_t count = UnwindStack(*context, frames, MaxStackFrames);
			if (count == 0)
			{
				out.Str("  <unwind produced no frames>\n");
				return;
			}

			for (size_t i = 0; i < count; ++i)
			{
				out.Str("  [");
				out.Dec(i);
				out.Str("] ");
				AppendResolvedAddress(out, frames[i]);
				out.Ch('\n');
			}
		}

		void AppendSrtState(Appender &out) noexcept
		{
			out.Str("\n--- SRT state ---\n");
			out.Str("Version        : ");
			out.View(SRTPluginRE9::Version::SemVer);
			out.Ch('\n');

			AppendBuildIdentity(out, "SRTPluginRE9.dll", g_selfIdentity);
			AppendBuildIdentity(out, "Host executable", g_exeIdentity);

			out.Str("Game version   : ");
			out.Str(g_CrashContext.GameVersionName[0] != '\0' ? g_CrashContext.GameVersionName : "<not detected>");
			out.Str("\nGame base      : 0x");
			out.Hex(g_CrashContext.GameBaseAddress, 16);
			out.Str("\nRankManager    : 0x");
			out.Hex(g_CrashContext.RankManager, 16);
			out.Str("\nCharacterMgr   : 0x");
			out.Hex(g_CrashContext.CharacterManager, 16);
			out.Str("\nRead loop iter : ");
			out.Dec(g_CrashContext.ReadLoopIterations.load(std::memory_order_relaxed));

			const uint32_t hookCount = g_CrashContext.HookCount.load(std::memory_order_acquire);
			out.Str("\nHooks recorded : ");
			out.Dec(hookCount);
			out.Str("\n\nHook targets. The owning module matters — another injector may have detoured\n"
			        "first, in which case we hooked their trampoline rather than the real function.\n"
			        "FAILED means SafetyHook returned an invalid hook and the detour is not live.\n");

			for (uint32_t i = 0; i < hookCount && i < MaxHookRecords; ++i)
			{
				out.Str(g_CrashContext.Hooks[i].Installed ? "  [ ok  ] " : "  [FAILED] ");
				out.Str(g_CrashContext.Hooks[i].Name);
				out.Str(" -> ");
				AppendResolvedAddress(out, g_CrashContext.Hooks[i].Target);
				out.Ch('\n');
			}
			if (hookCount == 0)
				out.Str("  <none attached>\n");
		}

		// Reads the snapshot taken earlier; makes no API calls of its own.
		void AppendModuleList(Appender &out) noexcept
		{
			// One row per module, so the compact form is the right one here: pdbkey is the
			// GUID and age concatenated, exactly as a symbol store names its directories.
			out.Str("\n--- Loaded modules (pdbkey = 32-hex GUID followed by age in hex) ---\n");

			if (g_moduleSnapshotCount == 0)
			{
				out.Str("  <module snapshot unavailable>\n");
				return;
			}

			for (uint32_t i = 0; i < g_moduleSnapshotCount; ++i)
			{
				const ModuleEntry &entry = g_moduleSnapshot[i];

				out.Str("  0x");
				out.Hex(entry.Base, 16);
				out.Str(" 0x");
				out.Hex(entry.Size, 8);
				out.Str(" ts=0x");
				out.Hex(entry.LinkTimestamp, 8);
				out.Str(" pdbkey=");
				if (entry.HasPdbId)
				{
					AppendGuid(out, entry.PdbGuid);
					out.Hex(entry.PdbAge, 0);
				}
				else
				{
					out.Str("<none>");
				}

				if (entry.Path[0] != L'\0')
				{
					out.Ch(' ');
					out.Wide(entry.Path);
				}
				out.Ch('\n');
			}
		}

		// Thread inventory only — per-thread stacks are in the .dmp. Suspending threads of a
		// dying process to unwind them risks deadlocking against a lock a suspended thread
		// holds, which would cost us the entire report for little added value.
		void AppendThreadInventory(Appender &out, DWORD faultingThreadId) noexcept
		{
			out.Str("\n--- Threads (per-thread stacks are in the .dmp) ---\n");

			const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
			if (snapshot == INVALID_HANDLE_VALUE)
			{
				out.Str("  <snapshot failed>\n");
				return;
			}

			const DWORD ownProcessId = GetCurrentProcessId();

			THREADENTRY32 entry{};
			entry.dwSize = sizeof(entry);
			if (Thread32First(snapshot, &entry) != FALSE)
			{
				do
				{
					if (entry.th32OwnerProcessID != ownProcessId)
						continue;

					out.Str("  TID ");
					out.Dec(entry.th32ThreadID);

					const HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ThreadID);
					if (thread != nullptr)
					{
						PWSTR description = nullptr;
						if (SUCCEEDED(GetThreadDescription(thread, &description)) && description != nullptr)
						{
							if (description[0] != L'\0')
							{
								out.Str("  \"");
								out.Wide(description);
								out.Ch('"');
							}
							LocalFree(description);
						}
						CloseHandle(thread);
					}

					if (entry.th32ThreadID == faultingThreadId)
						out.Str("   <<< FAULTING");
					out.Ch('\n');
				} while (Thread32Next(snapshot, &entry) != FALSE);
			}

			CloseHandle(snapshot);
		}

		// Each stage is flushed to disk as soon as it is built. The earlier stages are the ones
		// that matter most and the later ones are the ones most likely to deadlock (toolhelp
		// and the loader both take locks the faulting thread may already hold), so building
		// the whole report in memory and writing once at the end would risk losing everything
		// to a hang in the least important section.
		void FlushStage(Appender &out) noexcept
		{
			if (g_reportFile == INVALID_HANDLE_VALUE)
				return;

			const DWORD size = static_cast<DWORD>(out.Length(g_reportBuffer));
			if (size != 0)
			{
				DWORD written = 0;
				WriteFile(g_reportFile, g_reportBuffer, size, &written, nullptr);
				FlushFileBuffers(g_reportFile);
			}

			out.Cur = g_reportBuffer;
		}

		void WriteCrashReport(const CrashJob &job) noexcept
		{
			Appender out{g_reportBuffer, g_reportBuffer + ReportBufferSize - 1};

			SYSTEMTIME now{};
			GetSystemTime(&now);

			out.Str("SRTPluginRE9 crash report\n=========================\n\n");
			out.Str("Time (UTC)       : ");
			out.Dec(now.wYear);
			out.Ch('-');
			out.Dec2(now.wMonth);
			out.Ch('-');
			out.Dec2(now.wDay);
			out.Ch(' ');
			out.Dec2(now.wHour);
			out.Ch(':');
			out.Dec2(now.wMinute);
			out.Ch(':');
			out.Dec2(now.wSecond);
			out.Str("\nProcess ID       : ");
			out.Dec(GetCurrentProcessId());
			out.Str("\nFaulting thread  : ");
			out.Dec(job.FaultingThreadId);
			out.Str("\nMinidump         : ");
			out.Wide(job.DumpPath);
			if (!job.DumpWritten)
			{
				out.Str("   *** NOT WRITTEN, GetLastError=");
				out.Dec(job.DumpError);
				out.Str(" ***");
			}
			out.Ch('\n');
			FlushStage(out);

			// Stage 1 — the exception itself. No module resolution yet, so this cannot block.
			if (job.ExceptionInfo != nullptr && job.ExceptionInfo->ExceptionRecord != nullptr)
			{
				out.Ch('\n');
				AppendExceptionSummary(out, job.ExceptionInfo->ExceptionRecord);
			}
			if (job.ExceptionInfo != nullptr && job.ExceptionInfo->ContextRecord != nullptr)
				AppendRegisters(out, job.ExceptionInfo->ContextRecord);
			FlushStage(out);

			// Stage 2 — the one loader-lock pass. Everything after this resolves addresses
			// from the snapshot without touching the loader again.
			__try
			{
				TakeModuleSnapshot();
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				g_moduleSnapshotCount = 0;
			}

			if (job.ExceptionInfo != nullptr && job.ExceptionInfo->ExceptionRecord != nullptr)
			{
				out.Str("\nFault address    : ");
				AppendResolvedAddress(out, reinterpret_cast<uintptr_t>(job.ExceptionInfo->ExceptionRecord->ExceptionAddress));
				out.Ch('\n');
			}

			if (job.ExceptionInfo != nullptr && job.ExceptionInfo->ContextRecord != nullptr)
				AppendFaultingStack(out, job.ExceptionInfo->ContextRecord, job.FaultingThreadId);
			AppendSrtState(out);
			FlushStage(out);

			// Stage 3 — cheap and pure memcpy, so it goes before the risky inventory.
			out.Str("\n--- Recent log ---\n");
			out.Cur += SRTPluginRE9::Logger::CopyLogRing(out.Cur, out.Remaining());
			out.Ch('\n');
			FlushStage(out);

			AppendModuleList(out);
			FlushStage(out);

			// Stage 4 — toolhelp allocates from the process heap, so this is the section most
			// likely to hang after heap corruption. It is also the least important, which is
			// why it runs last.
			AppendThreadInventory(out, job.FaultingThreadId);
			out.Str("\n--- End of report ---\n");
			FlushStage(out);
		}

		// One short line for the main log. The full report goes to its own file — appending
		// all of it here would duplicate the log into itself.
		void AppendCrashLineToLog(const EXCEPTION_RECORD *record, const wchar_t *reportFileName, bool dumpWritten, DWORD dumpError) noexcept
		{
			char line[1024];
			Appender out{line, line + sizeof(line) - 1};

			out.Str("\nException ");
			out.Hex(record != nullptr ? record->ExceptionCode : 0u, 0);
			out.Str(" occurred at ");
			AppendResolvedAddress(out, record != nullptr ? reinterpret_cast<uintptr_t>(record->ExceptionAddress) : 0u);
			out.Str("\nCrash report: ");
			out.Wide(reportFileName);
			if (!dumpWritten)
			{
				out.Str("\nMiniDumpWriteDump FAILED, GetLastError=");
				out.Dec(dumpError);
			}
			out.Ch('\n');

			AppendToLogFile(line, static_cast<DWORD>(out.Length(line)));
		}

		// Runs on the crash worker, never on the faulting thread. Two reasons that matters:
		// a stack-overflow crash leaves the faulting thread with almost no stack and
		// MiniDumpWriteDump needs plenty, and dbghelp suspends every thread except its caller,
		// so dumping from elsewhere captures the faulting thread's real crash context instead
		// of a stack full of dbgcore frames.
		void WriteDumpAndReport(CrashJob &job) noexcept
		{
			BuildExtraMemoryRanges(job.ExceptionInfo != nullptr ? job.ExceptionInfo->ContextRecord : nullptr);

			// The dump comes first: it is the artifact we cannot regenerate.
			job.DumpWritten = false;
			job.DumpError = 0;

			const HANDLE dumpFile = CreateFileW(
			    job.DumpPath,
			    GENERIC_WRITE,
			    FILE_SHARE_READ,
			    nullptr,
			    CREATE_ALWAYS,
			    FILE_ATTRIBUTE_NORMAL,
			    nullptr);

			if (dumpFile == INVALID_HANDLE_VALUE)
			{
				job.DumpError = GetLastError();
			}
			else
			{
				MINIDUMP_EXCEPTION_INFORMATION exceptionParam{
				    .ThreadId = job.FaultingThreadId,
				    .ExceptionPointers = job.ExceptionInfo,
				    .ClientPointers = FALSE};

				// Each flag buys a specific debugger capability that MiniDumpNormal denied.
				// See docs/CRASH-REPORTS.md for the flag -> capability mapping.
				ULONG dumpType = static_cast<ULONG>(
				    MiniDumpWithDataSegs |                   // Globals.
				    MiniDumpWithHandleData |                 // !handle
				    MiniDumpWithUnloadedModules |            // Resolve IPs in unloaded DLLs.
				    MiniDumpWithIndirectlyReferencedMemory | // Follow stack/register pointers.
				    MiniDumpWithProcessThreadData |
				    MiniDumpWithThreadInfo |     // Thread start addresses and CPU times.
				    MiniDumpWithFullMemoryInfo | // !address
				    MiniDumpWithModuleHeaders |  // PE headers, so images needn't be on disk.
				    MiniDumpIgnoreInaccessibleMemory);

				// Code segments are what make the faulting instruction disassemblable, and
				// they are also the bulk of the file. The module callback confines them to the
				// game, this plugin and REFramework; the memory callback still captures the
				// fault site even when this is switched off.
				if (g_SRTSettings.CrashDumpIncludeCodeSegments != 0U)
					dumpType |= static_cast<ULONG>(MiniDumpWithCodeSegs);

				MINIDUMP_CALLBACK_INFORMATION callbackInfo{
				    .CallbackRoutine = &MiniDumpCallback,
				    .CallbackParam = nullptr};

				// Ship the recent log inside the dump so a reporter only sends one file.
				const size_t commentBytes = SRTPluginRE9::Logger::CopyLogRing(g_commentBuffer, sizeof(g_commentBuffer) - 1);
				g_commentBuffer[commentBytes] = '\0';

				MINIDUMP_USER_STREAM commentStream{
				    .Type = CommentStreamAType,
				    .BufferSize = static_cast<ULONG>(commentBytes + 1),
				    .Buffer = g_commentBuffer};

				MINIDUMP_USER_STREAM_INFORMATION userStreams{
				    .UserStreamCount = 1,
				    .UserStreamArray = &commentStream};

				job.DumpWritten = MiniDumpWriteDump(
				                      GetCurrentProcess(),
				                      GetCurrentProcessId(),
				                      dumpFile,
				                      static_cast<MINIDUMP_TYPE>(dumpType),
				                      &exceptionParam,
				                      &userStreams,
				                      &callbackInfo) != FALSE;

				if (!job.DumpWritten)
					job.DumpError = GetLastError();

				CloseHandle(dumpFile);
			}

			// Now the text report. If this faults, the dump is already safe on disk, and each
			// stage is flushed as it is produced so a later fault cannot discard earlier ones.
			__try
			{
				g_reportFile = CreateFileW(
				    job.ReportPath,
				    GENERIC_WRITE,
				    FILE_SHARE_READ,
				    nullptr,
				    CREATE_ALWAYS,
				    FILE_ATTRIBUTE_NORMAL,
				    nullptr);

				WriteCrashReport(job);

				if (g_reportFile != INVALID_HANDLE_VALUE)
				{
					CloseHandle(g_reportFile);
					g_reportFile = INVALID_HANDLE_VALUE;
				}

				AppendCrashLineToLog(
				    job.ExceptionInfo != nullptr ? job.ExceptionInfo->ExceptionRecord : nullptr,
				    job.ReportPath,
				    job.DumpWritten,
				    job.DumpError);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				if (g_reportFile != INVALID_HANDLE_VALUE)
				{
					CloseHandle(g_reportFile);
					g_reportFile = INVALID_HANDLE_VALUE;
				}
				static constexpr char kFallback[] = "\nSRT crash handler: report generation faulted; see the .dmp.\n";
				AppendToLogFile(kFallback, static_cast<DWORD>(sizeof(kFallback) - 1));
			}
		}

		DWORD WINAPI CrashWorkerMain(LPVOID) noexcept
		{
			// Named so it is obvious in the thread inventory and the dump's ThreadNames stream
			// which thread is dbghelp's and which one actually crashed.
			SetThreadDescription(GetCurrentThread(), L"RE9 SRT Crash Worker");

			// This thread exists to have stack available when the faulting thread may not.
			ReserveStackForCrashHandling();

			for (;;)
			{
				if (WaitForSingleObject(g_workerStart, INFINITE) != WAIT_OBJECT_0)
					return 0;

				__try
				{
					WriteDumpAndReport(g_job);
				}
				__except (EXCEPTION_EXECUTE_HANDLER)
				{
					// Nothing useful left to do; the filter's wait will still be released.
				}

				SetEvent(g_workerDone);
			}
		}

		LONG WINAPI SRTUnhandledExceptionFilter(EXCEPTION_POINTERS *exceptionInfo)
		{
			// If the filter itself faults, don't recurse — hand it to the next handler.
			if (g_inHandler.test_and_set(std::memory_order_acquire))
				return EXCEPTION_CONTINUE_SEARCH;

			// Reading KUSER_SHARED_DATA into a stack local. The filter runs on the faulting
			// thread's own stack with nothing unwound, so the stack is the most trustworthy
			// memory available here — no heap, no locks, no loader involvement.
			SYSTEMTIME now{};
			GetSystemTime(&now);

			g_job.ExceptionInfo = exceptionInfo;
			g_job.FaultingThreadId = GetCurrentThreadId();
			g_job.DumpWritten = false;
			g_job.DumpError = 0;
			BuildCrashFileName(now, L".dmp", g_job.DumpPath);
			BuildCrashFileName(now, L".crash.txt", g_job.ReportPath);

			if (g_workerThread != nullptr && g_workerStart != nullptr && g_workerDone != nullptr)
			{
				// Hand off and wait, bounded. If dbghelp deadlocks against a lock this thread
				// already holds, we give up waiting rather than hanging the game forever — the
				// dump is truncated but WER still gets its turn.
				SetEvent(g_workerStart);
				if (WaitForSingleObject(g_workerDone, CrashWorkTimeoutMs) != WAIT_OBJECT_0)
				{
					static constexpr char kTimeout[] = "\nSRT crash handler: dump/report timed out; artifacts may be truncated.\n";
					AppendToLogFile(kTimeout, static_cast<DWORD>(sizeof(kTimeout) - 1));
				}
			}
			else
			{
				// Worker unavailable (its creation failed at install). Do the work inline and
				// accept the risks the worker exists to avoid.
				__try
				{
					WriteDumpAndReport(g_job);
				}
				__except (EXCEPTION_EXECUTE_HANDLER)
				{
				}
			}

			// Let REFramework and WER see the exception too.
			return EXCEPTION_CONTINUE_SEARCH;
		}
	}

	// -------------------------------------------------------------------------------------
	// Public surface.
	// -------------------------------------------------------------------------------------

	BuildIdentity GetBuildIdentity(HMODULE module) noexcept
	{
		const auto *base = reinterpret_cast<const uint8_t *>(module != nullptr ? module : GetModuleHandleW(nullptr));
		if (base == nullptr)
			return {};
		return ReadBuildIdentity(base);
	}

	void RecordHook(const char *name, void *target) noexcept
	{
		const uint32_t index = g_CrashContext.HookCount.load(std::memory_order_relaxed);
		if (index >= MaxHookRecords || name == nullptr)
			return;

		size_t i = 0;
		while (i + 1 < sizeof(g_CrashContext.Hooks[index].Name) && name[i] != '\0')
		{
			g_CrashContext.Hooks[index].Name[i] = name[i];
			++i;
		}
		g_CrashContext.Hooks[index].Name[i] = '\0';
		g_CrashContext.Hooks[index].Target = reinterpret_cast<uintptr_t>(target);
		g_CrashContext.Hooks[index].Installed = false;
		g_CrashContext.HookCount.store(index + 1, std::memory_order_release);
	}

	void MarkHookInstalled(const char *name, bool installed) noexcept
	{
		if (name == nullptr)
			return;

		const uint32_t count = g_CrashContext.HookCount.load(std::memory_order_acquire);
		for (uint32_t index = 0; index < count && index < MaxHookRecords; ++index)
		{
			const char *a = g_CrashContext.Hooks[index].Name;
			const char *b = name;
			while (*a != '\0' && *a == *b)
			{
				++a;
				++b;
			}
			if (*a == '\0' && *b == '\0')
			{
				g_CrashContext.Hooks[index].Installed = installed;
				return;
			}
		}
	}

	void ReserveStackForCrashHandling() noexcept
	{
		ULONG stackBytes = StackGuaranteeBytes;
		SetThreadStackGuarantee(&stackBytes);
	}

	void LogBuildIdentity() noexcept
	{
		if (logger == nullptr)
			return;

		if (!g_selfIdentity.Valid)
		{
			logger->LogMessage("Build identity: <no CodeView record in SRTPluginRE9.dll>\n");
			return;
		}

		// Formatted by hand so the line reads exactly like a debugger's symbol identity,
		// which is what someone pastes into a bug report.
		char buffer[768];
		Appender out{buffer, buffer + sizeof(buffer) - 1};
		out.Str("Build identity: ");
		AppendPdbIdentity(out, g_selfIdentity.PdbGuid, g_selfIdentity.PdbAge);
		out.Str(", link timestamp 0x");
		out.Hex(g_selfIdentity.LinkTimestamp, 8);
		out.Str(", ");
		out.Str(g_selfIdentity.PdbPath);
		out.Ch('\n');
		*out.Cur = '\0';

		logger->LogMessage(std::string_view(buffer, out.Length(buffer)));
	}

	void Install(HMODULE module) noexcept
	{
		g_selfIdentity = GetBuildIdentity(module);
		g_exeIdentity = GetBuildIdentity(nullptr);

		ReserveStackForCrashHandling();

		// The worker is created now, while the process is healthy, and then parked. Creating
		// it at crash time would mean calling CreateThread from the filter — and CreateThread
		// takes the loader lock, which is one of the locks a crashing thread is most likely to
		// be holding. Parking a thread costs a handle and a stack; deadlocking there would
		// cost the dump.
		g_workerStart = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		g_workerDone = CreateEventW(nullptr, FALSE, FALSE, nullptr);

		if (g_workerStart != nullptr && g_workerDone != nullptr)
		{
			g_workerThread = CreateThread(nullptr, 0, &CrashWorkerMain, nullptr, 0, nullptr);
			if (g_workerThread == nullptr)
			{
				// Fall back to doing the work inline in the filter.
				CloseHandle(g_workerStart);
				CloseHandle(g_workerDone);
				g_workerStart = nullptr;
				g_workerDone = nullptr;
			}
		}

		SetUnhandledExceptionFilter(&SRTUnhandledExceptionFilter);
	}
}
