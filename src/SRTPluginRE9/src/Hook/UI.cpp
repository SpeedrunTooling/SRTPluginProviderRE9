#include "UI.h"
#include "CompositeOrderer.h"
#include "EnemyIds.h"
#include "GameObjects.h"
#include "Globals.h"
#include "Logo.h"
#include "Protected_Ptr.h"
#include "Render.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
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

	void STDMETHODCALLTYPE UI::RescaleDPI()
	{
		ImGuiStyle &style = ImGui::GetStyle() = ImGuiStyle(); // Reset style.
		ImGui::StyleColorsDark();                             // Set color mode again.
		style.ScaleAllSizes(g_SRTSettings.DPIScalingFactor);  // Set scaling.
		style.FontScaleDpi = g_SRTSettings.FontScalingFactor; // Set font DPI which is not set by the prior method call.
	}

	void STDMETHODCALLTYPE UI::RescaleFont()
	{
		ImGuiStyle &style = ImGui::GetStyle();                // Reset style.
		style.FontScaleDpi = g_SRTSettings.FontScalingFactor; // Set font DPI .
	}

	void STDMETHODCALLTYPE UI::DrawUI()
	{
		DrawLogoOverlay();
		DrawMain();

		if (debugLoggerOpen)
			DrawDebugLogger();

		if (overlayOpen)
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

	void STDMETHODCALLTYPE BarSizeSlider(const char *text, float &barSize, float &&minSize, float &&maxSize)
	{
		ImGui::SliderFloat(text, &barSize, minSize, maxSize, "%.0f");
	}

	void STDMETHODCALLTYPE UI::ToggleUI()
	{
		mainUIOpen = !mainUIOpen;
	}

	void STDMETHODCALLTYPE UI::DrawMain()
	{
		ImGuiIO &imguiIO = ImGui::GetIO();
		imguiIO.MouseDrawCursor = mainUIOpen;

		// If the Main UI is hidden, exit here.
		if (!mainUIOpen)
			return;

		// Conditionally shown items (shown only if the Main UI is showing)
		if (aboutUIOpen)
			DrawAbout();

		ImGui::SetNextWindowPos(ImVec2(imguiIO.DisplaySize.x / 4, imguiIO.DisplaySize.y / 4), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(400, 260), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowBgAlpha(g_SRTSettings.MainOpacity);

		if (!ImGui::Begin("SRT", (bool *)&mainUIOpen, ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoCollapse))
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
				ImGui::MenuItem("Log", NULL, &debugLoggerOpen);
				ImGui::MenuItem("Debug Overlay", NULL, &overlayOpen);
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Help"))
			{
				ImGui::MenuItem("About", NULL, &aboutUIOpen);
				ImGui::EndMenu();
			}
			ImGui::EndMenuBar();
		}

		ImGui::Text("Welcome to the beta build of RE9's SRT.");
		ImGui::Separator();
		ImGui::Text("Press F7 to toggle the main SRT window.");
		ImGui::Text("Press F8 or go to File -> Exit to shutdown the SRT.");
		ImGui::Separator();

		ImGui::Text("Resolution: %.0fx%.0f", horizontal, vertical);
		ImGui::Combo("Logo Position", &g_SRTSettings.LogoPosition, logoPositions, IM_ARRAYSIZE(logoPositions));
		OpacitySlider("Logo Opacity", g_SRTSettings.LogoOpacity, 10.0f);
		OpacitySlider("Main Opacity", g_SRTSettings.MainOpacity);
		OpacitySlider("About Opacity", g_SRTSettings.AboutOpacity);
		OpacitySlider("Logger Opacity", g_SRTSettings.LoggerOpacity);
		OpacitySlider("Overlay Opacity", g_SRTSettings.OverlayOpacity);

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

		ImGui::Checkbox("Show HP bars", &hpBarData.shouldShow);
		if (hpBarData.shouldShow)
		{
			ImGui::SameLine();
			ImGui::Checkbox("Show HP percent", &hpBarData.displayPercent);
			BarSizeSlider("Width", hpBarData.width, 20.0f, 300.0f);
			BarSizeSlider("Height", hpBarData.height, 2.0f, 30.0f);
		}
		ImGui::Checkbox("Hide full HP enemies", &hideFullHPEnemies);

		ImGui::End();
	}

	void STDMETHODCALLTYPE UI::DrawAbout()
	{
		// Specify a default position/size in case there's no data in the .ini file.
		ImGuiIO &io = ImGui::GetIO();
		ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x / 4, io.DisplaySize.y / 4), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowBgAlpha(g_SRTSettings.AboutOpacity);

		if (!ImGui::Begin("SRT: About", (bool *)&aboutUIOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::End();
			return;
		}

		ImGui::Text("Resident Evil 9 Requiem (2026) Speed Run Tool");
		ImGui::Text("v%s", SRTPluginRE9::Version::SemVer.data());
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
		if (!debugLoggerOpen)
			return;

		ImGuiWindowFlags window_flags = ImGuiWindowFlags_None;
		if (!mainUIOpen)
			window_flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;

		ImGui::SetNextWindowPos(ImVec2(250, 10), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(880, 440), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowBgAlpha(g_SRTSettings.LoggerOpacity);

		if (!ImGui::Begin("Logger", &debugLoggerOpen, window_flags))
		{
			ImGui::End();
			return;
		}

		// Options menu
		if (ImGui::BeginPopup("Options"))
		{
			ImGui::Checkbox("Auto-scroll", reinterpret_cast<bool *>(&g_SRTSettings.LoggerAutoScroll));
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
		debugLoggerFilter.Draw("Filter", -100.0f);

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
			if (debugLoggerFilter.IsActive())
			{
				for (int line_no = 0; line_no < localLoggerUIData.LineOffsets.Size; line_no++)
				{
					const char *line_start = buf + localLoggerUIData.LineOffsets[line_no];
					const char *line_end = line_no + 1 < localLoggerUIData.LineOffsets.Size ? buf + localLoggerUIData.LineOffsets[line_no + 1] - 1 : buf_end;
					if (debugLoggerFilter.PassFilter(line_start, line_end))
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
			if (g_SRTSettings.LoggerAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
				ImGui::SetScrollHereY(1.0f);
		}
		ImGui::EndChild();
		ImGui::End();
	}

	void STDMETHODCALLTYPE UI::DrawDebugOverlay()
	{
		ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
		if (!mainUIOpen)
			window_flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;
		ImGui::SetNextWindowPos(ImVec2(8, 145), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(240, 340), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowBgAlpha(g_SRTSettings.OverlayOpacity);
		if (ImGui::Begin("SRT Debug Overlay", &overlayOpen, window_flags))
		{
			auto readIndex = g_GameDataBufferReadIndex.load(std::memory_order_acquire);
			const auto &localGameData = g_GameDataBuffers[readIndex].Data;

			// DA
			ImGui::Text("Rank: %" PRIi32, localGameData.DARank);
			ImGui::Text("Points: %" PRIi32, localGameData.DAScore);

			// Player HP
			ImGui::Text("Player: %" PRIi32 " / %" PRIi32, localGameData.PlayerHP.CurrentHP, localGameData.PlayerHP.MaximumHP);
			ImGui::Separator();

			// Enemies
			const auto enemiesToShow = std::min(static_cast<size_t>(g_SRTSettings.EnemiesShownLimit), localGameData.FilteredEnemies.Size);
			ImGui::Text("Enemies (%zu of %zu):", enemiesToShow, localGameData.FilteredEnemies.Size);

			for (const auto &enemyData : std::span(static_cast<EnemyData *>(localGameData.FilteredEnemies.Values), localGameData.FilteredEnemies.Size) | std::views::take(g_SRTSettings.EnemiesShownLimit))
			{
				if (enemyData.HP.CurrentHP >= 1'000'000 || (hideFullHPEnemies && enemyData.HP.CurrentHP == enemyData.HP.MaximumHP))
				{
					continue;
				}
				auto name_display = enemies.contains(enemyData.KindID) ? enemies.at(enemyData.KindID) : std::format("{}", enemyData.KindID);
				if (enemyData.HP.CurrentHP != enemyData.HP.MaximumHP)
				{
					ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "%s %" PRIi32 " / %" PRIi32, name_display.c_str(), enemyData.HP.CurrentHP, enemyData.HP.MaximumHP);
				}
				else
				{
					ImGui::Text("%s %" PRIi32 " / %" PRIi32, name_display.c_str(), enemyData.HP.CurrentHP, enemyData.HP.MaximumHP);
				}

				if (hpBarData.shouldShow)
				{
					const auto hpPercent = enemyData.HP.MaximumHP > 0
					                            ? static_cast<float>(enemyData.HP.CurrentHP) / static_cast<float>(enemyData.HP.MaximumHP)
					                            : 0.0f;

					// Fill color
					ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.20f, 0.80f, 0.20f, 1.00f));

					// Back color
					ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.20f, 0.20f, 0.80f, 1.00f));

					// Scale by the font scale factor so that the bars remain proportional to the text size
					const auto width = hpBarData.width * g_SRTSettings.FontScalingFactor;
					const auto height = hpBarData.height * g_SRTSettings.FontScalingFactor;

					ImGui::ProgressBar(hpPercent, ImVec2(width, height), "");

					ImGui::PopStyleColor(2);

					if (hpBarData.displayPercent)
					{
						ImGui::SameLine();
						ImGui::Text("%.1f%%", hpPercent * 100.0f);
					}
				}
			}
		}
		ImGui::End();
	}

	void STDMETHODCALLTYPE UI::DrawLogoOverlay()
	{
		switch (static_cast<LogoPosition>(g_SRTSettings.LogoPosition))
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
			    ImVec4(1.0f, 1.0f, 1.0f, g_SRTSettings.LogoOpacity),
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

		if (g_SRTSettings.DPIScalingFactor == 0.f)
			g_SRTSettings.DPIScalingFactor = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));
		if (g_SRTSettings.FontScalingFactor == 0.f)
			g_SRTSettings.FontScalingFactor = g_SRTSettings.DPIScalingFactor;
		UI::RescaleDPI();
	}
}
