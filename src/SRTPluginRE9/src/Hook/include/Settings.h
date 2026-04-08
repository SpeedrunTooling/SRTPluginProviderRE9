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
	float_t LogoOpacity = .2f;
	uint32_t MainUIOpened = 1U; // true
	float_t MainOpacity = .85f;
	uint32_t AboutUIOpened = 1U; // true
	float_t AboutOpacity = .85f;
	uint32_t LogViewerOpened = 0U; // false
	float_t LogViewerOpacity = .85f;
	uint32_t LogViewerAutoScroll = 1U; // true

	uint32_t OverlayGameInfoUIOpened = 1U; // true
	float_t OverlayGameInfoOpacity = .6f;

	uint32_t OverlayPlayerUIOpened = 1U; // true
	float_t OverlayPlayerOpacity = .6f;

	uint32_t OverlayInventoryUIOpened = 0U; // false
	float_t OverlayInventoryOpacity = .6f;

	uint32_t OverlayEnemiesUIOpened = 1U; // true
	float_t OverlayEnemiesOpacity = .6f;

	uint32_t ShowCustomizationOptions = 1U; // true
	int32_t EnemiesShownLimit = 12;
	int32_t EnemiesFullHPTextColorIndex = 3;  // white
	int32_t EnemiesInjuredTextColorIndex = 1; // red
	uint32_t EnemiesHideFullHP = 1U;          // true
	uint32_t EnemyHPBarsVisible = 1U;         // true
	uint32_t EnemyHPBarsShowPercent = 0U;     // false
	float_t EnemiesMaxDistance = 50.f;
	float_t EnemyHPBarsWidth = 150.f;
	float_t EnemyHPBarsHeight = 5.f;
	int32_t EnemyHPBarForeColorIndex = 2; // green
	int32_t EnemyHPBarBackColorIndex = 4; // grey
	uint32_t DarkenBarColors = 1U;        // true

	float_t DPIScalingFactor = 0.f;  // Default to 0 so automatic calculation occurs. See UI.cpp.
	float_t FontScalingFactor = 0.f; // Default to 0 so automatic calculation occurs. See UI.cpp.

	// Debug settings, not shown in UI.
	uint32_t DebugEnable = 0U;                // Enables debug mode. Shows debug options in the settings. Default: false.
	uint32_t DebugPlayerShowPosition = 0U;    // Debug option to show player position. Default: false.
	uint32_t DebugEnemiesShowNotSpawned = 0U; // Debug option to show enemies that haven't spawned yet but are in the enemies array. Default: false.
	uint32_t DebugEnemiesShowPosition = 0U;   // Debug option to show enemy position and distance from player. Default: false.
	uint32_t DebugEnemiesShowID = 0U;         // Debug option to show the enemy's ID. Default: false.
	uint32_t DebugEnemiesShowKindID = 0U;     // Debug option to show the enemy's Kind ID. Default: false.
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
