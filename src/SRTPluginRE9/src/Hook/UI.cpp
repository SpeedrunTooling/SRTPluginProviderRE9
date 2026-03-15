#include "UI.h"
#include "CompositeOrderer.h"
#include "GameObjects.h"
#include "Globals.h"
#include "Logo.h"
#include "Protected_Ptr.h"
#include "Render.h"
#include "imgui.h"
#include <algorithm>
#include <cinttypes>
#include <functional>
#include <mutex>
#include <optional>
#include <ranges>

namespace SRTPluginRE9::Hook
{
	UI::UI()
	{
		logger->LogMessage("UI::ctor() - Allocating on SRV heap for logo.\n");
		logoHandle = g_dx12HookState.heaps.srv.Allocate();
		logoWidth = g_srtLogo_width;
		logoHeight = g_srtLogo_height;
		logger->LogMessage("UI::ctor() - Allocated handle {:p} on SRV heap.\n", reinterpret_cast<void *>(logoHandle.cpu.ptr));

		auto ret = LoadTextureFromMemory(
		    g_srtLogo.data(),
		    logoWidth,
		    logoHeight,
		    g_dx12HookState.device.Get(),
		    logoHandle.cpu,
		    &logoTexture);
		{
			ret;
			IM_ASSERT(ret);
		}

		DesktopResized();
	}

	UI::~UI()
	{
		DestroyTexture(&logoTexture);
	}

	void STDMETHODCALLTYPE UI::DrawUI()
	{
		ImGuiIO &imguiIO = ImGui::GetIO();
		imguiIO.FontGlobalScale = 1.33f;

		DrawLogoOverlay();
		DrawMain();

		if (debugLoggerUIOptions.Open)
			DrawDebugLogger();

		if (debugOverlayUIOptions.Open)
			DrawDebugOverlay();
	}

	void STDMETHODCALLTYPE UI::BadPointerReport(std::atomic<uint32_t> &ato, const std::function<bool(void)> &predicate, const std::function<void(void)> &function)
	{
		if (predicate())
		{
			auto current = ato.fetch_add(1U) + 1U;
			if (current >= triggerInterval)
			{
				auto expected = current;
				if (ato.compare_exchange_weak(expected, 0U))
					function();
			}
		}
	}

	void STDMETHODCALLTYPE OpacitySlider(const char *text, float &opacity, float &&minimumOpacity = 5.0f, float &&maximumOpacity = 100.0f)
	{
		auto opacityDisplay = opacity * 100.0f;
		if (ImGui::SliderFloat(text, &opacityDisplay, minimumOpacity, maximumOpacity, "%.0f%%"))
			opacity = opacityDisplay / 100.0f;
	}

	void STDMETHODCALLTYPE UI::ToggleUI()
	{
		mainUIOptions.Open = !mainUIOptions.Open;
	}

	void STDMETHODCALLTYPE UI::DrawMain()
	{
		ImGuiIO &imguiIO = ImGui::GetIO();
		imguiIO.MouseDrawCursor = mainUIOptions.Open;

		// If the Main UI is hidden, exit here.
		if (!mainUIOptions.Open)
			return;

		// Conditionally shown items (shown only if the Main UI is showing)
		if (aboutUIOptions.Open)
			DrawAbout();

		ImGui::SetNextWindowPos(ImVec2(imguiIO.DisplaySize.x / 4, imguiIO.DisplaySize.y / 4), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(400, 260), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowBgAlpha(mainUIOptions.Opacity);

		if (!ImGui::Begin("SRT", (bool *)&mainUIOptions.Open, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoCollapse))
		{
			ImGui::End();
			return;
		}

		// Menu Bar
		if (ImGui::BeginMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
				if (ImGui::MenuItem("Exit", NULL, false, true))
				{
					// Close the SRT.
					g_shutdownRequested.store(true);
				}
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("View"))
			{
				ImGui::MenuItem("Log", NULL, &debugLoggerUIOptions.Open);
				ImGui::MenuItem("Debug Overlay", NULL, &debugOverlayUIOptions.Open);
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Help"))
			{
				ImGui::MenuItem("About", NULL, &aboutUIOptions.Open);
				ImGui::EndMenu();
			}
			ImGui::EndMenuBar();
		}

		ImGui::Text("Welcome to the beta build of RE9's SRT.");
		ImGui::Separator();
		ImGui::Text("Press F7 to toggle the main SRT window.");
		ImGui::Text("Press F8 or go to File -> Exit to shutdown the SRT.");
		ImGui::Separator();

		OpacitySlider("Main Opacity", mainUIOptions.Opacity);
		OpacitySlider("About Opacity", aboutUIOptions.Opacity);
		OpacitySlider("Logger Opacity", debugLoggerUIOptions.Opacity);
		OpacitySlider("Overlay Opacity", debugOverlayUIOptions.Opacity);
		OpacitySlider("Logo Opacity", logoOptions.Opacity, 10.0f);
		ImGui::Combo("Logo Position", reinterpret_cast<int32_t *>(&logoOptions.Position), logoPositions, IM_ARRAYSIZE(logoPositions));
		ImGui::Text("Resolution: %.0fx%.0f", horizontal, vertical);

		ImGui::End();
	}

	void STDMETHODCALLTYPE UI::DrawAbout()
	{
		// Specify a default position/size in case there's no data in the .ini file.
		ImGuiIO &io = ImGui::GetIO();
		ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x / 4, io.DisplaySize.y / 4), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowBgAlpha(aboutUIOptions.Opacity);

		if (!ImGui::Begin("SRT: About", (bool *)&aboutUIOptions.Open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::End();
			return;
		}

		ImGui::Text("Resident Evil 9 Requiem (2026) Speed Run Tool");
		// ImGui::Text("v%s", SRTPluginRE9::Version::SemVer.data());
		ImGui::Separator();
		ImGui::BulletText("Contributors\n\tSquirrelies\n\tHntd");
		ImGui::Spacing();
		ImGui::Spacing();
		bool copyToClipboard = ImGui::Button("Copy to clipboard");
		ImGui::Spacing();
		if (ImGui::BeginChild("buildInfo", ImVec2(0, 0), ImGuiChildFlags_FrameStyle | ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysAutoResize))
		{
			if (copyToClipboard)
			{
				ImGui::LogToClipboard();
				ImGui::LogText("```\n"); // Back quotes will make text appears without formatting when pasting on GitHub
			}

			ImGui::Text("Resident Evil 9 Requiem (2026) Speed Run Tool");
			// ImGui::Text("v%s", SRTPluginRE9::Version::SemVer.data());
			ImGui::Separator();
			ImGui::Text("Build datetime: %s %s", __DATE__, __TIME__);
			// ImGui::Text("Debug build: %s", SRTPluginRE9::IsDebug ? "true" : "false");
			ImGui::Text("sizeof(void *): %d", (int)sizeof(void *));
#ifdef _WIN32
			ImGui::Text("define: _WIN32");
#endif
#ifdef _WIN64
			ImGui::Text("define: _WIN64");
#endif
			ImGui::Text("define: __cplusplus=%d", (int)__cplusplus);
#ifdef __STDC__
			ImGui::Text("define: __STDC__=%d", (int)__STDC__);
#endif
#ifdef __STDC_VERSION__
			ImGui::Text("define: __STDC_VERSION__=%d", (int)__STDC_VERSION__);
#endif
#ifdef __GNUC__
			ImGui::Text("define: __GNUC__=%d", (int)__GNUC__);
#endif
#ifdef __clang_version__
			ImGui::Text("define: __clang_version__=%s", __clang_version__);
#endif

#ifdef _MSC_VER
			ImGui::Text("define: _MSC_VER=%d", _MSC_VER);
#endif
#ifdef _MSVC_LANG
			ImGui::Text("define: _MSVC_LANG=%d", (int)_MSVC_LANG);
#endif
#ifdef __MINGW32__
			ImGui::Text("define: __MINGW32__");
#endif
#ifdef __MINGW64__
			ImGui::Text("define: __MINGW64__");
#endif

			if (copyToClipboard)
			{
				ImGui::LogText("\n```");
				ImGui::LogFinish();
			}
			ImGui::EndChild();
		}

		ImGui::End();
	}

	void STDMETHODCALLTYPE UI::DrawDebugLogger()
	{
		static auto clearFunc = [this]()
		{
			std::lock_guard<std::mutex> lock(g_LogMutex);
			if (g_LoggerUIData)
			{
				g_LoggerUIData->Buffer.clear();
				g_LoggerUIData->LineOffsets.clear();
				g_LoggerUIData->LineOffsets.push_back(0);
			}
		};

		// Don't continue if we're not open.
		if (!debugLoggerUIOptions.Open)
			return;

		ImGuiWindowFlags window_flags = ImGuiWindowFlags_None;
		if (!mainUIOptions.Open)
			window_flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;

		ImGui::SetNextWindowPos(ImVec2(250, 10), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(880, 440), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowBgAlpha(debugLoggerUIOptions.Opacity);

		if (!ImGui::Begin("Logger", &debugLoggerUIOptions.Open, window_flags))
		{
			ImGui::End();
			return;
		}

		// Options menu
		if (ImGui::BeginPopup("Options"))
		{
			ImGui::Checkbox("Auto-scroll", &debugLoggerUIOptions.AutoScroll);
			ImGui::EndPopup();
		}

		// Main window
		if (ImGui::Button("Options"))
			ImGui::OpenPopup("Options");
		ImGui::SameLine();
		const bool clear = ImGui::Button("Clear");
		ImGui::SameLine();
		const bool copy = ImGui::Button("Copy");
		ImGui::SameLine();
		debugLoggerUIOptions.Filter.Draw("Filter", -100.0f);

		ImGui::Separator();

		if (ImGui::BeginChild("scrolling", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
		{
			SRTPluginRE9::Logger::LoggerUIData localLoggerUIData;
			{
				std::lock_guard<std::mutex> lock(g_LogMutex);
				localLoggerUIData = *g_LoggerUIData;
			}

			if (clear)
				clearFunc();
			if (copy)
				ImGui::LogToClipboard();

			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
			const char *buf = localLoggerUIData.Buffer.begin();
			const char *buf_end = localLoggerUIData.Buffer.end();
			if (debugLoggerUIOptions.Filter.IsActive())
			{
				for (int line_no = 0; line_no < localLoggerUIData.LineOffsets.Size; line_no++)
				{
					const char *line_start = buf + localLoggerUIData.LineOffsets[line_no];
					const char *line_end = line_no + 1 < localLoggerUIData.LineOffsets.Size ? buf + localLoggerUIData.LineOffsets[line_no + 1] - 1 : buf_end;
					if (debugLoggerUIOptions.Filter.PassFilter(line_start, line_end))
						ImGui::TextUnformatted(line_start, line_end);
				}
			}
			else
			{
				ImGuiListClipper clipper;
				clipper.Begin(localLoggerUIData.LineOffsets.Size);
				while (clipper.Step())
				{
					for (int line_no = clipper.DisplayStart; line_no < clipper.DisplayEnd; line_no++)
					{
						const char *line_start = buf + localLoggerUIData.LineOffsets[line_no];
						const char *line_end = line_no + 1 < localLoggerUIData.LineOffsets.Size ? buf + localLoggerUIData.LineOffsets[line_no + 1] - 1 : buf_end;
						ImGui::TextUnformatted(line_start, line_end);
					}
				}
				clipper.End();
			}
			ImGui::PopStyleVar();

			// Keep up at the bottom of the scroll region if we were already at the bottom at the beginning of the frame.
			// Using a scrollbar or mouse-wheel will take away from the bottom edge.
			if (debugLoggerUIOptions.AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
				ImGui::SetScrollHereY(1.0f);
		}
		ImGui::EndChild();
		ImGui::End();
	}

	void STDMETHODCALLTYPE UI::DrawDebugOverlay()
	{
		ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
		if (!mainUIOptions.Open)
			window_flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;
		ImGui::SetNextWindowPos(ImVec2(8, 145), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(240, 340), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowBgAlpha(debugOverlayUIOptions.Opacity);
		if (ImGui::Begin("SRT Debug Overlay", &debugOverlayUIOptions.Open, window_flags))
		{
			// Game data section
			auto rankManager = protect(reinterpret_cast<RankManager **>(*g_BaseAddress + 0x0E815400ULL)).deref();
			auto characterManager = protect(reinterpret_cast<CharacterManager **>(*g_BaseAddress + 0x0E843CF8ULL)).deref();
			auto gameClock = protect(reinterpret_cast<GameClock **>(*g_BaseAddress + 0x0E834680ULL)).deref();
			// auto cameraSystem = protect(reinterpret_cast<CameraSystem **>(*g_BaseAddress + 0x0E816138ULL)).deref();

			// DA
			auto activeRankProfile = rankManager.follow(&RankManager::_ActiveRankProfile);
			auto daRank = activeRankProfile.read(&RankProfile::_CurrentRank);
			auto daScore = activeRankProfile.read(&RankProfile::_RankPoints);
			ImGui::Text("Rank: %" PRIi32, daRank);
			ImGui::Text("Points: %" PRIi32, daScore);

			// BadPointerReport(
			//     reportedBadDA,
			//     [=]()
			//     {
			// 	    return daRank == 0 && daScore == 0;
			//     },
			//     [=]()
			//     {
			// 	    logger->LogMessage("Potentially bad pointer detected: DA {} / {}\n", daRank, daScore);
			// 	    logger->LogMessage("RankManager: {:p}\n", reinterpret_cast<void *>(rankManager.get()));
			// 	    logger->LogMessage("RankProfile: {:p}\n", reinterpret_cast<void *>(activeRankProfile.get()));
			//     });

			// Player HP
			auto activePlayerContext = characterManager.follow(&CharacterManager::PlayerContextFast);
			auto playerHitPoint = activePlayerContext.follow(&PlayerContext::HitPoint);
			auto playerHitPointData = playerHitPoint.follow(&HitPoint::HitPointData);
			auto playerCurrentHP = playerHitPointData.read(&CharacterHitPointData::_CurrentHP);
			auto playerMaxHP = playerHitPointData.read(&CharacterHitPointData::_CurrentMaxHP);
			ImGui::Text("Player: %" PRIi32 " / %" PRIi32, playerCurrentHP, playerMaxHP);
			ImGui::Separator();

			// BadPointerReport(
			//     reportedBadPlayerHP,
			//     [=]()
			//     {
			// 	    return playerCurrentHP == 0 && playerMaxHP == 0;
			//     },
			//     [=]()
			//     {
			// 	    logger->LogMessage("Potentially bad pointer detected: Player HP {} / {}\n", playerCurrentHP, playerMaxHP);
			// 	    logger->LogMessage("CharacterManager: {:p}\n", reinterpret_cast<void *>(characterManager.get()));
			// 	    logger->LogMessage("PlayerContextFast: {:p}\n", reinterpret_cast<void *>(activePlayerContext.get()));
			// 	    logger->LogMessage("PlayerHitPoint: {:p}\n", reinterpret_cast<void *>(playerHitPoint.get()));
			// 	    logger->LogMessage("PlayerHitPointData: {:p}\n", reinterpret_cast<void *>(playerHitPointData.get()));
			//     });

			// Enemy HP
			auto enemyContextList = characterManager.follow(&CharacterManager::EnemyContextList);
			if (enemyContextList)
			{
				// Grabs all enemy context data we care about whose pointers are not null, then condenses them into a smaller struct for us to work over.
				auto localList = std::span<EnemyContext *>(enemyContextList->begin(), enemyContextList->end()) |
				                 std::views::filter([](const EnemyContext *enemyContext)
				                                    { return enemyContext && enemyContext->HitPoint && enemyContext->HitPoint->HitPointData; }) |
				                 std::views::transform([](const EnemyContext *enemyContext)
				                                       { return CondensedEnemyInfo{
					                                         .KindID = enemyContext->KindID,
					                                         .CurrentHP = enemyContext->HitPoint->HitPointData->_CurrentHP,
					                                         .CurrentMaxHP = enemyContext->HitPoint->HitPointData->_CurrentMaxHP,
					                                         .IsSetup = enemyContext->HitPoint->HitPointData->_IsSetuped != 0}; });

				// Applies user-defined filtering.
				auto userFilteredList = localList |
				                        std::views::filter([](const CondensedEnemyInfo &enemyContext)
				                                           { return enemyContext.CurrentMaxHP >= 2 && enemyContext.CurrentHP != 0; }) |
				                        std::ranges::to<std::vector>();

				// Applies user-defined sorting.
				auto compare = OrderByDescending([](const CondensedEnemyInfo &enemyInfo)
				                                 { return enemyInfo.CurrentHP < enemyInfo.CurrentMaxHP; })
				                   .ThenByDescending([](const CondensedEnemyInfo &enemyInfo)
				                                     { return enemyInfo.CurrentMaxHP; });
				std::ranges::sort(userFilteredList, compare);

				// Iterates over the results for display.
				auto filteredEnemyCount = userFilteredList.size();
				ImGui::Text("Enemies (%zu of %zu):", std::min(enemyCountLimit, filteredEnemyCount), filteredEnemyCount);
				for (const auto &enemyContext : userFilteredList | std::views::take(enemyCountLimit))
				{
					if (enemyContext.CurrentHP != enemyContext.CurrentMaxHP)
						ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "%" PRIi32 " / %" PRIi32, enemyContext.CurrentHP, enemyContext.CurrentMaxHP);
					else
						ImGui::Text("%" PRIi32 " / %" PRIi32, enemyContext.CurrentHP, enemyContext.CurrentMaxHP);
				}
			}

			// IGT
			// auto timers = gameClock.follow(&GameClock::_Timers);
			// if (timers)
			// {
			// 	auto count = static_cast<size_t>(timers->Count());
			// 	for (size_t i = 0ULL; i < count; ++i)
			// 	{
			// 		auto time = timers.follow<Time>(offsetof(ManagedArray<Time *>, _Values), i, count);
			// 		if (time)
			// 		{
			// 			auto elapsed = time.read(&Time::_ElapsedTime);
			// 			ImGui::Text("Timer[%" PRIu32 "]: elapsed = %" PRIu64, i, elapsed);
			// 		}
			// 	}
			// }
		}
		ImGui::End();
	}

	void STDMETHODCALLTYPE UI::DrawLogoOverlay()
	{
		switch (logoOptions.Position)
		{
			case LogoPosition::UpperLeft:
			default:
				ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
				break;
			case LogoPosition::UpperRight:
				ImGui::SetNextWindowPos(ImVec2(horizontal - 50.0f - static_cast<float>(logoWidth), 10.0f), ImGuiCond_Always);
				break;
			case LogoPosition::LowerLeft:
				ImGui::SetNextWindowPos(ImVec2(10.0f, vertical - 50.0f - static_cast<float>(logoHeight)), ImGuiCond_Always);
				break;
			case LogoPosition::LowerRight:
				ImGui::SetNextWindowPos(ImVec2(horizontal - 50.0f - static_cast<float>(logoWidth), vertical - 50.0f - static_cast<float>(logoHeight)), ImGuiCond_Always);
				break;
		}

		if (ImGui::Begin("Logo", nullptr, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove))
		{
			ImGui::Image(
			    logoHandle.gpu.ptr,
			    ImVec2(static_cast<float>(logoWidth) * logoScaleFactor, static_cast<float>(logoHeight) * logoScaleFactor),
			    ImVec2(0, 0),
			    ImVec2(1, 1),
			    ImVec4(1.0f, 1.0f, 1.0f, logoOptions.Opacity),
			    ImVec4(0, 0, 0, 0));
		}
		ImGui::End();
	}

	void STDMETHODCALLTYPE UI::DesktopResized()
	{
		const HWND hDesktop = GetDesktopWindow();
		RECT desktop;
		GetWindowRect(hDesktop, &desktop);
		horizontal = static_cast<float>(desktop.right);
		vertical = static_cast<float>(desktop.bottom);
	}
}
