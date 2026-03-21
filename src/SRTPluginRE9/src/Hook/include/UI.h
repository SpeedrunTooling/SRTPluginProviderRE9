#ifndef SRTPLUGINRE9_UI_H
#define SRTPLUGINRE9_UI_H

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "DescriptorHeapAllocator.h"
#include "Settings.h"
#include "imgui.h"
#include <atomic>
#include <cstdint>
#include <d3d12.h>
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

	struct HPBarData
	{
		bool shouldShow;
		bool displayPercent;
		float width;
		float height;
	};

	class UI
	{
	public:
		explicit UI();
		~UI();
		void STDMETHODCALLTYPE DrawUI();
		void STDMETHODCALLTYPE ToggleUI();
		void STDMETHODCALLTYPE DesktopResized();

	private:
		void STDMETHODCALLTYPE RescaleDPI();
		void STDMETHODCALLTYPE RescaleFont();
		void STDMETHODCALLTYPE BadPointerReport(std::atomic<uint32_t> &, const std::function<bool(void)> &, const std::function<void(void)> &);

		void STDMETHODCALLTYPE DrawMain();
		void STDMETHODCALLTYPE DrawAbout();
		void STDMETHODCALLTYPE DrawDebugLogger();
		void STDMETHODCALLTYPE DrawDebugOverlay();
		void STDMETHODCALLTYPE DrawLogoOverlay();

		float horizontal;
		float vertical;

		ImGuiTextFilter debugLoggerFilter;
		bool debugLoggerOpen = false; // g_SRTSettings
		bool overlayOpen = true;
		bool mainUIOpen = true;
		bool aboutUIOpen = false;
		HPBarData hpBarData{.shouldShow = false, .displayPercent = false, .width = 100.0f, .height = 10.0f};

		const char *logoPositions[4]{"Upper Left", "Upper Right", "Lower Left", "Lower Right"};
		SRTPluginRE9::Hook::DescriptorHandle logoHandle;
		ID3D12Resource *logoTexture = nullptr;
		int32_t logoWidth = 0;
		int32_t logoHeight = 0;
		float logoScaleFactor = .5f;
		bool hideFullHPEnemies = false;

		std::atomic<uint32_t> reportedBadDA = 0;
		std::atomic<uint32_t> reportedBadPlayerHP = 0;
		static const uint32_t triggerInterval = 120U * 20U; // (120 * 20) = approximately how many frames we wait before we trigger a bad pointer report. Just in case we were loading a new zone.
	};
}

#endif
