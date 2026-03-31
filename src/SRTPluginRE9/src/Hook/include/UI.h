#ifndef SRTPLUGINRE9_UI_H
#define SRTPLUGINRE9_UI_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Settings.h"
#include "imgui.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <windows.h>

namespace SRTPluginRE9::Hook
{
	enum class LogoPosition : int32_t
	{
		UpperLeft = 0,
		UpperRight = 1,
		LowerLeft = 2,
		LowerRight = 3
	};

	enum class ColorPreset : int32_t
	{
		Blue = 0,
		Red = 1,
		Green = 2,
		White = 3,
		Gray = 4
	};

	class UI
	{
	public:
		explicit UI();
		void STDMETHODCALLTYPE DrawUI();
		void STDMETHODCALLTYPE ToggleUI();
		static void STDMETHODCALLTYPE RescaleDPI();
		static void STDMETHODCALLTYPE RescaleFont();
		void STDMETHODCALLTYPE GameWindowResized();

	private:
		void STDMETHODCALLTYPE BadPointerReport(std::atomic<uint32_t> &, const std::function<bool(void)> &, const std::function<void(void)> &);

		void STDMETHODCALLTYPE DrawMain();
		void STDMETHODCALLTYPE DrawAbout();
		void STDMETHODCALLTYPE DrawDebugLogger();
		void STDMETHODCALLTYPE DrawDebugOverlay();
		void STDMETHODCALLTYPE DrawLogoOverlay();
		void STDMETHODCALLTYPE RenderHPBar(int32_t currentHP, int32_t maximumHP);
		ImVec4 STDMETHODCALLTYPE ColorFromPreset(int32_t preset);

		float horizontal;
		float vertical;

		ImGuiTextFilter debugLoggerFilter;

		const char *logoPositions[4]{"Upper Left", "Upper Right", "Lower Left", "Lower Right"};
		const char *colorPresets[5]{"Blue", "Red", "Green", "White", "Gray"};
		float logoScaleFactor = .5f;

		std::atomic<uint32_t> reportedBadDA = 0;
		std::atomic<uint32_t> reportedBadPlayerHP = 0;
		static const uint32_t triggerInterval = 120U * 20U; // (120 * 20) = approximately how many frames we wait before we trigger a bad pointer report. Just in case we were loading a new zone.
	};
}

#endif
