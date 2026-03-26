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
	int32_t LogoPosition = 0; // Top-left
	float LogoOpacity = .2f;
	float MainOpacity = .85f;
	float AboutOpacity = .85f;
	float LoggerOpacity = .85f;
	uint32_t LoggerAutoScroll = 1U; // true
	float OverlayOpacity = .6f;

	uint32_t ShowCustomizationOptions = 1U; // true
	int EnemiesShownLimit = 12;
	int32_t EnemiesFullHPTextColorIndex = 3;  // white
	int32_t EnemiesInjuredTextColorIndex = 1; // red
	uint32_t EnemiesHideFullHP = 1U;          // true
	uint32_t EnemyHPBarsVisible = 1U;         // true
	uint32_t EnemyHPBarsShowPercent = 0U;     // false
	float EnemyHPBarsWidth = 150.f;
	float EnemyHPBarsHeight = 5.f;
	int32_t EnemyHPBarForeColorIndex = 2; // green
	int32_t EnemyHPBarBackColorIndex = 4; // grey
	uint32_t DarkenBarColors = 1U;        // true

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
