#include "UI.h"
#include "CharacterMap.h"
#include "CompositeOrderer.h"
#include "DX12Hook.h"
#include "GUID.h"
#include "GameObjects.h"
#include "Globals.h"
#include "Protected_Ptr.h"
#include <algorithm>
#include <cinttypes>
#include <functional>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <mutex>
#include <optional>
#include <ranges>

namespace SRTPluginRE9::Hook
{
	UI::UI()
	{
		logger->LogMessage("UI::UI() called.\n");
		GameWindowResized();
	}

	void STDMETHODCALLTYPE UI::DrawUI()
	{
		DrawLogoOverlay();
		DrawMain();
		DrawOverlayGameInfo();
		DrawOverlayPlayer();
		DrawOverlayInventory();
		DrawOverlayEnemies();
		DrawLogViewer();
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

	void STDMETHODCALLTYPE BarSizeSlider(const char *text, float &barSize, float &&minSize, float &&maxSize)
	{
		ImGui::SliderFloat(text, &barSize, minSize, maxSize, "%.0f");
	}

	void STDMETHODCALLTYPE UI::ToggleUI()
	{
		g_SRTSettings.MainUIOpened = !g_SRTSettings.MainUIOpened;
	}

	void STDMETHODCALLTYPE UI::DrawMain()
	{
		ImGuiIO &imguiIO = ImGui::GetIO();
		imguiIO.MouseDrawCursor = g_SRTSettings.MainUIOpened;

		// If the Main UI is hidden, exit here.
		if (!g_SRTSettings.MainUIOpened)
			return;

		// Conditionally shown items (shown only if the Main UI is showing)
		if (g_SRTSettings.AboutUIOpened)
			DrawAbout();

		ImGui::SetNextWindowPos(ImVec2(imguiIO.DisplaySize.x / 16, imguiIO.DisplaySize.y / 16), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowBgAlpha(g_SRTSettings.MainOpacity);

		// "Display Title###Unique Window ID"
		static const std::string mainWindowTitle = std::format("{} - v{}###SRTMain", SRTPluginRE9::ToolNameShort, SRTPluginRE9::Version::SemVer);
		if (!ImGui::Begin(mainWindowTitle.c_str(), reinterpret_cast<bool *>(&g_SRTSettings.MainUIOpened), ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoCollapse))
		{
			ImGui::End();
			return;
		}

		// Menu Bar
		if (ImGui::BeginMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
				ImGui::MenuItem("Enable Debug Mode", NULL, reinterpret_cast<bool *>(&g_SRTSettings.DebugEnable));
				if (ImGui::MenuItem("Exit", NULL, false, true))
				{
					// Close the SRT.
					g_shutdownRequested.store(true);
				}
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("View"))
			{
				ImGui::MenuItem("Log Viewer", NULL, reinterpret_cast<bool *>(&g_SRTSettings.LogViewerOpened));
				if (ImGui::BeginMenu("Overlays"))
				{
					ImGui::MenuItem("Game Info", NULL, reinterpret_cast<bool *>(&g_SRTSettings.OverlayGameInfoUIOpened));
					ImGui::MenuItem("Player Info", NULL, reinterpret_cast<bool *>(&g_SRTSettings.OverlayPlayerUIOpened));
					ImGui::MenuItem("Inventory", NULL, reinterpret_cast<bool *>(&g_SRTSettings.OverlayInventoryUIOpened));
					ImGui::MenuItem("Enemies", NULL, reinterpret_cast<bool *>(&g_SRTSettings.OverlayEnemiesUIOpened));
					ImGui::EndMenu();
				}
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Help"))
			{
				ImGui::MenuItem("About", NULL, reinterpret_cast<bool *>(&g_SRTSettings.AboutUIOpened));
				ImGui::EndMenu();
			}
			ImGui::EndMenuBar();
		}

		ImGui::Text("Thank you for using the %s for %s.", SRTPluginRE9::ToolNameShort, SRTPluginRE9::GameName);
		ImGui::Separator();
		ImGui::Text("Press F7 to toggle the main %s window.", SRTPluginRE9::ToolNameShort);
		ImGui::Text("Press F8 or go to File -> Exit to shutdown the %s.", SRTPluginRE9::ToolNameShort);
		ImGui::Separator();

		ImGui::Text("Resolution: %.0fx%.0f", horizontal, vertical);
		ImGui::Combo("Logo Position", &g_SRTSettings.LogoPosition, logoPositions, IM_ARRAYSIZE(logoPositions));
		OpacitySlider("Logo Opacity", g_SRTSettings.LogoOpacity, 10.0f);
		OpacitySlider("Main Opacity", g_SRTSettings.MainOpacity);
		OpacitySlider("About Opacity", g_SRTSettings.AboutOpacity);
		OpacitySlider("Log Viewer Opacity", g_SRTSettings.LogViewerOpacity);
		OpacitySlider("Overlay Game Info Opacity", g_SRTSettings.OverlayGameInfoOpacity);
		OpacitySlider("Overlay Player Info Opacity", g_SRTSettings.OverlayPlayerOpacity);
		OpacitySlider("Overlay Inventory Opacity", g_SRTSettings.OverlayInventoryOpacity);
		OpacitySlider("Overlay Enemies Opacity", g_SRTSettings.OverlayEnemiesOpacity);

		// Enemy Count Slider
		ImGui::SliderInt("Limit Enemies Shown", &g_SRTSettings.EnemiesShownLimit, 1, 32, "%d");

		// DPI Scale Slider.
		{
			auto floatDisplay = g_SRTSettings.DPIScalingFactor * 100.0f;
			if (ImGui::SliderFloat("DPI Scaling Factor", &floatDisplay, 75.0f, 300.0f, "%.0f%%"))
			{
				g_SRTSettings.DPIScalingFactor = floatDisplay / 100.0f;
				UI::RescaleDPI();
			}
		}

		// Font Scale Slider.
		{
			auto floatDisplay = g_SRTSettings.FontScalingFactor * 100.0f;
			if (ImGui::SliderFloat("Font Scaling Factor", &floatDisplay, 75.0f, 300.0f, "%.0f%%"))
			{
				g_SRTSettings.FontScalingFactor = floatDisplay / 100.0f;
				UI::RescaleFont();
			}
		}

		ImGui::Checkbox("Show customization options", reinterpret_cast<bool *>(&g_SRTSettings.ShowCustomizationOptions));
		if (g_SRTSettings.ShowCustomizationOptions)
		{
			ImGui::Checkbox("Hide full HP enemies", reinterpret_cast<bool *>(&g_SRTSettings.EnemiesHideFullHP));
			if (!g_SRTSettings.EnemiesHideFullHP)
				ImGui::Combo("Full HP enemy text color", &g_SRTSettings.EnemiesFullHPTextColorIndex, colorPresets, IM_ARRAYSIZE(colorPresets));
			ImGui::Combo("Injured enemy text color", &g_SRTSettings.EnemiesInjuredTextColorIndex, colorPresets, IM_ARRAYSIZE(colorPresets));

			ImGui::Checkbox("Show HP bars", reinterpret_cast<bool *>(&g_SRTSettings.EnemyHPBarsVisible));
			if (g_SRTSettings.EnemyHPBarsVisible)
			{
				ImGui::SameLine();
				ImGui::Checkbox("Show HP percent", reinterpret_cast<bool *>(&g_SRTSettings.EnemyHPBarsShowPercent));
				BarSizeSlider("Width", g_SRTSettings.EnemyHPBarsWidth, 20.0f, 300.0f);
				BarSizeSlider("Height", g_SRTSettings.EnemyHPBarsHeight, 2.0f, 30.0f);
				ImGui::Combo("Foreground color", &g_SRTSettings.EnemyHPBarForeColorIndex, colorPresets, IM_ARRAYSIZE(colorPresets));
				ImGui::Combo("Background color", &g_SRTSettings.EnemyHPBarBackColorIndex, colorPresets, IM_ARRAYSIZE(colorPresets));
				ImGui::Checkbox("Darken bar colors", reinterpret_cast<bool *>(&g_SRTSettings.DarkenBarColors));
			}
			ImGui::SliderFloat("Enemy distance filter", &g_SRTSettings.EnemiesMaxDistance, 1.f, 250.f, "%5.1f");

			if (g_SRTSettings.DebugEnable)
			{
				ImGui::Checkbox("Show non-spawned enemies", reinterpret_cast<bool *>(&g_SRTSettings.DebugEnemiesShowNotSpawned));
				ImGui::Checkbox("Show enemy position and distance", reinterpret_cast<bool *>(&g_SRTSettings.DebugEnemiesShowPosition));
				ImGui::Checkbox("Show enemy id", reinterpret_cast<bool *>(&g_SRTSettings.DebugEnemiesShowID));
				ImGui::Checkbox("Show enemy kind id", reinterpret_cast<bool *>(&g_SRTSettings.DebugEnemiesShowKindID));
			}
		}

		if (g_SRTSettings.DebugEnable)
		{
			ImGui::Checkbox("Show player position", reinterpret_cast<bool *>(&g_SRTSettings.DebugPlayerShowPosition));
		}

		ImGui::End();
	}

	void STDMETHODCALLTYPE UI::DrawAbout()
	{
		// Specify a default position/size in case there's no data in the .ini file.
		ImGuiIO &io = ImGui::GetIO();
		ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x / 4, io.DisplaySize.y / 16), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowBgAlpha(g_SRTSettings.AboutOpacity);

		static const std::string aboutWindowTitle = std::format("{}: About###SRTAbout", SRTPluginRE9::ToolNameShort);
		if (!ImGui::Begin(aboutWindowTitle.c_str(), reinterpret_cast<bool *>(&g_SRTSettings.AboutUIOpened), ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::End();
			return;
		}

		ImGui::Text("%s %s", SRTPluginRE9::GameName, SRTPluginRE9::ToolName);
		ImGui::Text("v%s", SRTPluginRE9::Version::SemVer.data());
		ImGui::Separator();
		ImGui::BulletText("Contributors\n\tSquirrelies\n\tHntd\n\tJaxon\n\tKestrel");
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

			ImGui::Text("%s %s", SRTPluginRE9::GameName, SRTPluginRE9::ToolName);
			ImGui::Text("v%s", SRTPluginRE9::Version::SemVer.data());
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

	void STDMETHODCALLTYPE UI::DrawLogViewer()
	{
		static auto clearFunc = [this]()
		{
			std::lock_guard<std::mutex> lock(g_LogMutex);
			if (g_LogViewerData)
			{
				g_LogViewerData->Buffer.clear();
				g_LogViewerData->LineOffsets.clear();
				g_LogViewerData->LineOffsets.push_back(0);
			}
		};

		// Don't continue if we're not open.
		if (!g_SRTSettings.LogViewerOpened)
			return;

		ImGuiWindowFlags window_flags = ImGuiWindowFlags_None;
		if (!g_SRTSettings.MainUIOpened)
			window_flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;

		ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowBgAlpha(g_SRTSettings.LogViewerOpacity);

		static const std::string logViewerWindowTitle = std::format("{} Log Viewer###SRTLogViewer", SRTPluginRE9::ToolNameShort);
		if (!ImGui::Begin(logViewerWindowTitle.c_str(), reinterpret_cast<bool *>(&g_SRTSettings.LogViewerOpened), window_flags))
		{
			ImGui::End();
			return;
		}

		// Options menu
		if (ImGui::BeginPopup("Options"))
		{
			ImGui::Checkbox("Auto-scroll", reinterpret_cast<bool *>(&g_SRTSettings.LogViewerAutoScroll));
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
		logViewerFilter.Draw("Filter", -100.0f);

		ImGui::Separator();

		if (ImGui::BeginChild("scrolling", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
		{
			SRTPluginRE9::Logger::LogViewerData localLogViewerData;
			{
				std::lock_guard<std::mutex> lock(g_LogMutex);
				localLogViewerData = *g_LogViewerData;
			}

			if (clear)
				clearFunc();
			if (copy)
				ImGui::LogToClipboard();

			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
			const char *buf = localLogViewerData.Buffer.begin();
			const char *buf_end = localLogViewerData.Buffer.end();
			if (logViewerFilter.IsActive())
			{
				for (int line_no = 0; line_no < localLogViewerData.LineOffsets.Size; line_no++)
				{
					const char *line_start = buf + localLogViewerData.LineOffsets[line_no];
					const char *line_end = line_no + 1 < localLogViewerData.LineOffsets.Size ? buf + localLogViewerData.LineOffsets[line_no + 1] - 1 : buf_end;
					if (logViewerFilter.PassFilter(line_start, line_end))
						ImGui::TextUnformatted(line_start, line_end);
				}
			}
			else
			{
				ImGuiListClipper clipper;
				clipper.Begin(localLogViewerData.LineOffsets.Size);
				while (clipper.Step())
				{
					for (int line_no = clipper.DisplayStart; line_no < clipper.DisplayEnd; line_no++)
					{
						const char *line_start = buf + localLogViewerData.LineOffsets[line_no];
						const char *line_end = line_no + 1 < localLogViewerData.LineOffsets.Size ? buf + localLogViewerData.LineOffsets[line_no + 1] - 1 : buf_end;
						ImGui::TextUnformatted(line_start, line_end);
					}
				}
				clipper.End();
			}
			ImGui::PopStyleVar();

			// Keep up at the bottom of the scroll region if we were already at the bottom at the beginning of the frame.
			// Using a scrollbar or mouse-wheel will take away from the bottom edge.
			if (g_SRTSettings.LogViewerAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
				ImGui::SetScrollHereY(1.0f);
		}
		ImGui::EndChild();
		ImGui::End();
	}

	void STDMETHODCALLTYPE UI::DrawOverlayGameInfo()
	{
		// Don't continue if we're not open.
		if (!g_SRTSettings.OverlayGameInfoUIOpened)
			return;

		ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
		if (!g_SRTSettings.MainUIOpened)
			window_flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;
		ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(240, 340), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowBgAlpha(g_SRTSettings.OverlayGameInfoOpacity);

		static const std::string overlayGameInfoWindowTitle = std::format("{} Game Info Overlay###SRTOverlayGameInfo", SRTPluginRE9::ToolNameShort);
		if (ImGui::Begin(overlayGameInfoWindowTitle.c_str(), reinterpret_cast<bool *>(&g_SRTSettings.OverlayGameInfoUIOpened), window_flags))
		{
			auto readIndex = g_GameDataBufferReadIndex.load(std::memory_order_acquire);
			const auto &localBufferedGameData = g_GameDataBuffers[readIndex];

			if (!localBufferedGameData.HasData)
				ImGui::Text("SRT is loading...");
			else
			{
				const auto &localGameData = localBufferedGameData.Data;

				ImGui::Text("Rank: %" PRIi32, localGameData.DARank);
				ImGui::Text("Points: %" PRIi32, localGameData.DAScore);
			}
		}
		ImGui::End();
	}

	void STDMETHODCALLTYPE UI::DrawOverlayPlayer()
	{
		// Don't continue if we're not open.
		if (!g_SRTSettings.OverlayPlayerUIOpened)
			return;

		ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
		if (!g_SRTSettings.MainUIOpened)
			window_flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;
		ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(240, 340), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowBgAlpha(g_SRTSettings.OverlayPlayerOpacity);

		static const std::string overlayPlayerWindowTitle = std::format("{} Player Overlay###SRTOverlayPlayer", SRTPluginRE9::ToolNameShort);
		if (ImGui::Begin(overlayPlayerWindowTitle.c_str(), reinterpret_cast<bool *>(&g_SRTSettings.OverlayPlayerUIOpened), window_flags))
		{
			auto readIndex = g_GameDataBufferReadIndex.load(std::memory_order_acquire);
			const auto &localBufferedGameData = g_GameDataBuffers[readIndex];

			if (!localBufferedGameData.HasData)
				ImGui::Text("SRT is loading...");
			else
			{
				const auto &localGameData = localBufferedGameData.Data;

				auto playerName = characterMap.contains(localGameData.Player.KindID) ? characterMap.at(localGameData.Player.KindID) : localGameData.Player.KindID;
				ImGui::Text("%s: %" PRIi32 " / %" PRIi32, playerName.c_str(), localGameData.Player.HP.CurrentHP, localGameData.Player.HP.MaximumHP);

				if (g_SRTSettings.DebugEnable && g_SRTSettings.DebugPlayerShowPosition)
				{
					ImGui::Text("X: %6.2f", localGameData.Player.Position.X);
					ImGui::Text("Y: %6.2f", localGameData.Player.Position.Y);
					ImGui::Text("Z: %6.2f", localGameData.Player.Position.Z);
				}
			}
		}
		ImGui::End();
	}

	void STDMETHODCALLTYPE UI::DrawOverlayInventory()
	{
		// Don't continue if we're not open.
		if (!g_SRTSettings.OverlayInventoryUIOpened)
			return;

		ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
		if (!g_SRTSettings.MainUIOpened)
			window_flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;
		ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(240, 340), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowBgAlpha(g_SRTSettings.OverlayInventoryOpacity);

		static const std::string overlayInventoryWindowTitle = std::format("{} Inventory Overlay###SRTOverlayInventory", SRTPluginRE9::ToolNameShort);
		if (ImGui::Begin(overlayInventoryWindowTitle.c_str(), reinterpret_cast<bool *>(&g_SRTSettings.OverlayInventoryUIOpened), window_flags))
		{
			auto readIndex = g_GameDataBufferReadIndex.load(std::memory_order_acquire);
			const auto &localBufferedGameData = g_GameDataBuffers[readIndex];

			if (!localBufferedGameData.HasData)
				ImGui::Text("SRT is loading...");
			else
			{
				// const auto &localGameData = localBufferedGameData.Data;

				ImGui::Text("Placeholder!");
			}
		}
		ImGui::End();
	}

	void STDMETHODCALLTYPE UI::DrawOverlayEnemies()
	{
		// Don't continue if we're not open.
		if (!g_SRTSettings.OverlayEnemiesUIOpened)
			return;

		ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
		if (!g_SRTSettings.MainUIOpened)
			window_flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;
		ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(240, 340), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowBgAlpha(g_SRTSettings.OverlayEnemiesOpacity);

		static const std::string overlayEnemiesWindowTitle = std::format("{} Enemies Overlay###SRTOverlayEnemies", SRTPluginRE9::ToolNameShort);
		if (ImGui::Begin(overlayEnemiesWindowTitle.c_str(), reinterpret_cast<bool *>(&g_SRTSettings.OverlayEnemiesUIOpened), window_flags))
		{
			auto readIndex = g_GameDataBufferReadIndex.load(std::memory_order_acquire);
			const auto &localBufferedGameData = g_GameDataBuffers[readIndex];

			if (!localBufferedGameData.HasData)
				ImGui::Text("SRT is loading...");
			else
			{
				const auto &localGameData = localBufferedGameData.Data;

				// Enemies
				const auto enemiesToShow = std::min(static_cast<size_t>(g_SRTSettings.EnemiesShownLimit), localGameData.FilteredEnemies.Size);
				ImGui::Text("Enemies (%zu of %zu):", enemiesToShow, localGameData.FilteredEnemies.Size);

				for (const auto &enemyData : std::span(static_cast<EnemyData *>(localGameData.FilteredEnemies.Values), localGameData.FilteredEnemies.Size) | std::views::take(g_SRTSettings.EnemiesShownLimit))
				{
					if (g_SRTSettings.EnemiesHideFullHP && enemyData.HP.CurrentHP == enemyData.HP.MaximumHP)
						continue;

					ImVec4 enemyColor;
					if (enemyData.HP.CurrentHP != enemyData.HP.MaximumHP)
						enemyColor = ColorFromPreset(g_SRTSettings.EnemiesInjuredTextColorIndex);
					else
						enemyColor = ColorFromPreset(g_SRTSettings.EnemiesFullHPTextColorIndex);

					auto enemyName = characterMap.contains(enemyData.KindID) ? characterMap.at(enemyData.KindID) : enemyData.KindID;

					// ImGui::TextColored(enemyColor, "%s (%s) [%s]", enemyName.c_str(), enemyData.KindID.c_str(), SRTPluginRE9::Guid::ToString(enemyData.ID).c_str());
					if (g_SRTSettings.DebugEnable)
					{
						if (g_SRTSettings.DebugEnemiesShowID && g_SRTSettings.DebugEnemiesShowKindID)
							ImGui::TextColored(enemyColor, "%s (%s) [%s]", enemyName.c_str(), enemyData.KindID.c_str(), SRTPluginRE9::Guid::ToString(enemyData.ID).c_str());
						else if (!g_SRTSettings.DebugEnemiesShowID && g_SRTSettings.DebugEnemiesShowKindID)
							ImGui::TextColored(enemyColor, "%s (%s)", enemyName.c_str(), enemyData.KindID.c_str());
						else if (g_SRTSettings.DebugEnemiesShowID && !g_SRTSettings.DebugEnemiesShowKindID)
							ImGui::TextColored(enemyColor, "%s [%s]", enemyName.c_str(), SRTPluginRE9::Guid::ToString(enemyData.ID).c_str());
						else
							ImGui::TextColored(enemyColor, "%s", enemyName.c_str());
					}
					else
						ImGui::TextColored(enemyColor, "%s", enemyName.c_str());

					ImGui::TextColored(enemyColor, "%" PRIi32 " / %" PRIi32, enemyData.HP.CurrentHP, enemyData.HP.MaximumHP);

					if (g_SRTSettings.DebugEnable && g_SRTSettings.DebugEnemiesShowPosition)
					{
						if (enemyData.IsSpawned)
						{
							ImVec4 distanceColor;
							if (enemyData.Distance > 50.f)
								distanceColor = ImVec4{0.2f, 0.8f, 0.2f, 1.f}; // Green
							else if (enemyData.Distance > 25.f && enemyData.Distance <= 50.f)
								distanceColor = ImVec4{0.8f, 0.8f, 0.2f, 1.f}; // Yellow
							else
								distanceColor = ImVec4{0.8f, 0.2f, 0.2f, 1.f}; // Red
							auto distanceString = std::format("Pos: {:.2f}, {:.2f}, {:.2f} -> {:.2f}", enemyData.Position.X, enemyData.Position.Y, enemyData.Position.Z, enemyData.Distance);
							ImGui::TextColored(distanceColor, distanceString.c_str());
						}
						else
							ImGui::Text("Not Spawned In");
					}

					if (g_SRTSettings.EnemyHPBarsVisible)
						RenderHPBar(enemyData.HP.CurrentHP, enemyData.HP.MaximumHP);
				}
			}
		}
		ImGui::End();
	}

	void STDMETHODCALLTYPE UI::DrawLogoOverlay()
	{
		const auto &hookState = DX12Hook::DX12Hook::GetInstance().GetHookState();
		switch (static_cast<LogoPosition>(g_SRTSettings.LogoPosition))
		{
			case LogoPosition::UpperLeft:
			default:
				ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
				break;
			case LogoPosition::UpperRight:
				ImGui::SetNextWindowPos(ImVec2(horizontal - 50.0f - static_cast<float>(hookState.logoWidth), 10.0f), ImGuiCond_Always);
				break;
			case LogoPosition::LowerLeft:
				ImGui::SetNextWindowPos(ImVec2(10.0f, vertical - 50.0f - static_cast<float>(hookState.logoHeight)), ImGuiCond_Always);
				break;
			case LogoPosition::LowerRight:
				ImGui::SetNextWindowPos(ImVec2(horizontal - 50.0f - static_cast<float>(hookState.logoWidth), vertical - 50.0f - static_cast<float>(hookState.logoHeight)), ImGuiCond_Always);
				break;
		}

		static const std::string logoWindowTitle = std::format("{} Logo###SRTLogo", SRTPluginRE9::ToolNameShort);
		if (ImGui::Begin(logoWindowTitle.c_str(), nullptr, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove))
		{
			ImGui::Image(
			    hookState.logoHandle.gpu.ptr,
			    ImVec2(static_cast<float>(hookState.logoWidth) * logoScaleFactor, static_cast<float>(hookState.logoHeight) * logoScaleFactor),
			    ImVec2(0, 0),
			    ImVec2(1, 1),
			    ImVec4(1.0f, 1.0f, 1.0f, g_SRTSettings.LogoOpacity),
			    ImVec4(0, 0, 0, 0));
		}
		ImGui::End();
	}

	void STDMETHODCALLTYPE UI::RenderHPBar(int32_t curHP, int32_t maxHP)
	{
		const auto hpPercent = maxHP > 0
		                           ? static_cast<float>(curHP) / static_cast<float>(maxHP)
		                           : 0.0f;

		// Fill color
		ImVec4 foreColor = ColorFromPreset(g_SRTSettings.EnemyHPBarForeColorIndex);
		if (g_SRTSettings.DarkenBarColors)
		{
			foreColor.x *= 0.7f;
			foreColor.y *= 0.7f;
			foreColor.z *= 0.7f;
		}
		ImGui::PushStyleColor(ImGuiCol_PlotHistogram, foreColor);

		// Back color
		ImVec4 backColor = ColorFromPreset(g_SRTSettings.EnemyHPBarBackColorIndex);
		if (g_SRTSettings.DarkenBarColors)
		{
			backColor.x *= 0.7f;
			backColor.y *= 0.7f;
			backColor.z *= 0.7f;
		}
		ImGui::PushStyleColor(ImGuiCol_FrameBg, backColor);

		// Scale by the font scale factor so that the bars remain proportional to the text size
		const auto width = g_SRTSettings.EnemyHPBarsWidth * g_SRTSettings.FontScalingFactor;
		const auto height = g_SRTSettings.EnemyHPBarsHeight * g_SRTSettings.FontScalingFactor;

		ImGui::ProgressBar(hpPercent, ImVec2(width, height), "");

		ImGui::PopStyleColor(2);

		if (g_SRTSettings.EnemyHPBarsShowPercent)
		{
			ImGui::SameLine();
			ImGui::Text("%.1f%%", hpPercent * 100.0f);
		}
	}

	ImVec4 STDMETHODCALLTYPE UI::ColorFromPreset(int32_t preset)
	{
		switch (static_cast<ColorPreset>(preset))
		{
			case ColorPreset::Blue:
				return ImVec4(0.2f, 0.2f, 0.8f, 1.0f);
			case ColorPreset::Red:
				return ImVec4(0.8f, 0.2f, 0.2f, 1.0f);
			case ColorPreset::Green:
				return ImVec4(0.2f, 0.8f, 0.2f, 1.0f);
			case ColorPreset::Gray:
				return ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
		}

		// Return white as default
		return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	}

	void STDMETHODCALLTYPE UI::RescaleDPI()
	{
		if (g_SRTSettings.DPIScalingFactor == 0.f)
			g_SRTSettings.DPIScalingFactor = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));

		ImGuiStyle &style = ImGui::GetStyle() = ImGuiStyle(); // Reset style.
		ImGui::StyleColorsDark();                             // Set color mode again.
		style.ScaleAllSizes(g_SRTSettings.DPIScalingFactor);  // Set scaling.
		UI::RescaleFont();                                    // Set font size and DPI which is not set by the prior method call.
	}

	void STDMETHODCALLTYPE UI::RescaleFont()
	{
		if (g_SRTSettings.FontScalingFactor == 0.f)
			g_SRTSettings.FontScalingFactor = g_SRTSettings.DPIScalingFactor;

		ImGuiStyle &style = ImGui::GetStyle();                // Reset style.
		style.FontSizeBase = 14.0f;                           // Set a default font size.
		style.FontScaleDpi = g_SRTSettings.FontScalingFactor; // Set font DPI.
	}

	void STDMETHODCALLTYPE UI::GameWindowResized()
	{
		RECT gameWindowSize;
		GetWindowRect(DX12Hook::DX12Hook::GetInstance().GetHookState().gameWindow, &gameWindowSize);
		horizontal = static_cast<float>(gameWindowSize.right);
		vertical = static_cast<float>(gameWindowSize.bottom);

		UI::RescaleDPI();
	}
}
