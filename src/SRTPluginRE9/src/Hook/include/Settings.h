#ifndef SRTPLUGINRE9_SETTINGS_H
#define SRTPLUGINRE9_SETTINGS_H

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "imgui_internal.h"
#include <concepts>
#include <cstdint>
#include <functional>
#include <string_view>

struct SRTSettings
{
	int32_t LogoPosition = 0;
	float LogoOpacity = .2f;

	float MainOpacity = 1.f;

	float AboutOpacity = 1.f;

	float LoggerOpacity = .2f;
	uint32_t LoggerAutoScroll = 1U; // true

	float OverlayOpacity = .2f;

	uint32_t ShowCustomizationOptions = 0U; // false
	int EnemiesShownLimit = 16;
	uint32_t EnemiesHideFullHP = 1U;      // true
	int32_t EnemiesFullHPTextColorIndex = 4; // white
	int32_t EnemiesInjuredTextColorIndex = 1; // red
	uint32_t EnemyHPBarsVisible = 0U;     // false
	uint32_t EnemyHPBarsShowPercent = 0U; // false
	float EnemyHPBarsWidth = 100.f;
	float EnemyHPBarsHeight = 10.f;
	int32_t EnemyHPBarForeColorIndex = 2; // green
	int32_t EnemyHPBarBackColorIndex = 0; // blue
	uint32_t DarkenBarColors = 0U; // false

	float DPIScalingFactor = 0.f;
	float FontScalingFactor = 0.f;
};

template <typename TSettingType>
concept NumericSettingType = std::integral<TSettingType> || std::floating_point<TSettingType>;

template <NumericSettingType TSettingType>
static bool TryReadSetting(const std::string_view &inputStringView, const std::string_view &&settingName, TSettingType &settingValue);

void RegisterSRTSettingsHandler();
static void *SRTSettings_ReadOpen(ImGuiContext *, ImGuiSettingsHandler *, const char *);
static void SRTSettings_ReadLine(ImGuiContext *, ImGuiSettingsHandler *, void *, const char *);
static void SRTSettings_WriteAll(ImGuiContext *, ImGuiSettingsHandler *, ImGuiTextBuffer *);

#endif
