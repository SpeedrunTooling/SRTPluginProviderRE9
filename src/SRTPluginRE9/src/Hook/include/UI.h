#ifndef SRTPLUGINRE9_UI_H
#define SRTPLUGINRE9_UI_H

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "DescriptorHeapAllocator.h"
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

	struct UIOption
	{
		float Opacity;
		bool Open;
	};

	struct LoggerUIOption : UIOption
	{
		ImGuiTextFilter Filter;
		bool AutoScroll;
	};

	struct LogoOption
	{
		float Opacity;
		LogoPosition Position;
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
		void STDMETHODCALLTYPE BadPointerReport(std::atomic<uint32_t> &, const std::function<bool(void)> &, const std::function<void(void)> &);

		void STDMETHODCALLTYPE DrawMain();
		void STDMETHODCALLTYPE DrawAbout();
		void STDMETHODCALLTYPE DrawDebugLogger();
		void STDMETHODCALLTYPE DrawDebugOverlay();
		void STDMETHODCALLTYPE DrawLogoOverlay();

		UIOption mainUIOptions{.Opacity = 1.f, .Open = true};
		UIOption aboutUIOptions{.Opacity = 1.f, .Open = false};
		LoggerUIOption debugLoggerUIOptions{.2f, false, {}, true};
		UIOption debugOverlayUIOptions{.Opacity = .2f, .Open = true};
		LogoOption logoOptions{.Opacity = .2f, .Position = LogoPosition::UpperLeft};

		float horizontal;
		float vertical;

		const char *logoPositions[4]{"Upper Left", "Upper Right", "Lower Left", "Lower Right"};
		SRTPluginRE9::Hook::DescriptorHandle logoHandle;
		ID3D12Resource *logoTexture = nullptr;
		int32_t logoWidth = 0;
		int32_t logoHeight = 0;
		float logoScaleFactor = .5f;

		std::atomic<uint32_t> reportedBadDA = 0;
		std::atomic<uint32_t> reportedBadPlayerHP = 0;
		static const uint32_t triggerInterval = 120U * 20U; // (120 * 20) = approximately how many frames we wait before we trigger a bad pointer report. Just in case we were loading a new zone.
		static const size_t enemyCountLimit = 16;
	};
}

#endif
