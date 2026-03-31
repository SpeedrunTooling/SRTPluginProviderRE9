#include "Settings.h"
#include "Globals.h"
#include <charconv>
#include <cstdio>

void RegisterSRTSettingsHandler()
{
	ImGuiSettingsHandler handler;
	handler.TypeName = "SRTSettings";            // Section name in .ini
	handler.TypeHash = ImHashStr("SRTSettings"); // Must match TypeName
	handler.ClearAllFn = NULL;                   // Optional: clear all data
	handler.ReadOpenFn = SRTSettings_ReadOpen;
	handler.ReadLineFn = SRTSettings_ReadLine;
	handler.WriteAllFn = SRTSettings_WriteAll;
	ImGui::GetCurrentContext()->SettingsHandlers.push_back(handler);
}

static void *SRTSettings_ReadOpen(ImGuiContext *, ImGuiSettingsHandler *, const char * /*name*/)
{
	return &g_SRTSettings;
}

template <NumericSettingType TSettingType>
static bool TryReadSetting(const std::string_view &inputStringView, const std::string_view &&settingName, TSettingType &settingValue)
{
	assert(settingName.ends_with("="));
	if (inputStringView.starts_with(settingName))
	{
		auto value = inputStringView.substr(settingName.length());
		auto resultStatus = std::from_chars(value.data(), value.data() + value.size(), settingValue);
		return resultStatus.ec == std::errc();
	}

	return false;
}

static void SRTSettings_ReadLine(ImGuiContext *, ImGuiSettingsHandler *, void * /*settingsObjectEntry*/, const char *line)
{
	std::string inputStringView(line);

	bool readSuccess = false;
	if (!readSuccess)
		readSuccess = TryReadSetting(inputStringView, "LogoPosition=", g_SRTSettings.LogoPosition);
	if (!readSuccess)
	{
		readSuccess = TryReadSetting(inputStringView, "LogoOpacity=", g_SRTSettings.LogoOpacity);

		// Lock logo opacity within the range of 10% to 100%.
		if (g_SRTSettings.LogoOpacity < 0.1f)
			g_SRTSettings.LogoOpacity = 0.1f;
		else if (g_SRTSettings.LogoOpacity > 1.f)
			g_SRTSettings.LogoOpacity = 1.f;
	}

	if (!readSuccess)
		readSuccess = TryReadSetting(inputStringView, "MainUIOpened=", g_SRTSettings.MainUIOpened);
	if (!readSuccess)
		readSuccess = TryReadSetting(inputStringView, "MainOpacity=", g_SRTSettings.MainOpacity);

	if (!readSuccess)
		readSuccess = TryReadSetting(inputStringView, "AboutUIOpened=", g_SRTSettings.AboutUIOpened);
	if (!readSuccess)
		readSuccess = TryReadSetting(inputStringView, "AboutOpacity=", g_SRTSettings.AboutOpacity);

	if (!readSuccess)
		readSuccess = TryReadSetting(inputStringView, "LogViewerOpened=", g_SRTSettings.LogViewerOpened);
	if (!readSuccess)
		readSuccess = TryReadSetting(inputStringView, "LogViewerOpacity=", g_SRTSettings.LogViewerOpacity);
	if (!readSuccess)
		readSuccess = TryReadSetting(inputStringView, "LogViewerAutoScroll=", g_SRTSettings.LogViewerAutoScroll);

	if (!readSuccess)
		readSuccess = TryReadSetting(inputStringView, "OverlayGameInfoUIOpened=", g_SRTSettings.OverlayGameInfoUIOpened);
	if (!readSuccess)
		readSuccess = TryReadSetting(inputStringView, "OverlayGameInfoOpacity=", g_SRTSettings.OverlayGameInfoOpacity);

	if (!readSuccess)
		readSuccess = TryReadSetting(inputStringView, "OverlayPlayerUIOpened=", g_SRTSettings.OverlayPlayerUIOpened);
	if (!readSuccess)
		readSuccess = TryReadSetting(inputStringView, "OverlayPlayerOpacity=", g_SRTSettings.OverlayPlayerOpacity);

	if (!readSuccess)
		readSuccess = TryReadSetting(inputStringView, "OverlayInventoryUIOpened=", g_SRTSettings.OverlayInventoryUIOpened);
	if (!readSuccess)
		readSuccess = TryReadSetting(inputStringView, "OverlayInventoryOpacity=", g_SRTSettings.OverlayInventoryOpacity);

	if (!readSuccess)
		readSuccess = TryReadSetting(inputStringView, "OverlayEnemiesUIOpened=", g_SRTSettings.OverlayEnemiesUIOpened);
	if (!readSuccess)
		readSuccess = TryReadSetting(inputStringView, "OverlayEnemiesOpacity=", g_SRTSettings.OverlayEnemiesOpacity);

	if (!readSuccess)
		readSuccess = TryReadSetting(inputStringView, "ShowCustomizationOptions=", g_SRTSettings.ShowCustomizationOptions);
	if (!readSuccess)
		readSuccess = TryReadSetting(inputStringView, "EnemiesShownLimit=", g_SRTSettings.EnemiesShownLimit);
	if (!readSuccess)
		readSuccess = TryReadSetting(inputStringView, "EnemiesFullHPTextColorIndex=", g_SRTSettings.EnemiesFullHPTextColorIndex);
	if (!readSuccess)
		readSuccess = TryReadSetting(inputStringView, "EnemiesInjuredTextColorIndex=", g_SRTSettings.EnemiesInjuredTextColorIndex);
	if (!readSuccess)
		readSuccess = TryReadSetting(inputStringView, "EnemiesHideFullHP=", g_SRTSettings.EnemiesHideFullHP);
	if (!readSuccess)
		readSuccess = TryReadSetting(inputStringView, "EnemyHPBarsVisible=", g_SRTSettings.EnemyHPBarsVisible);
	if (!readSuccess)
		readSuccess = TryReadSetting(inputStringView, "EnemyHPBarsShowPercent=", g_SRTSettings.EnemyHPBarsShowPercent);
	if (!readSuccess)
		readSuccess = TryReadSetting(inputStringView, "EnemiesMaxDistance=", g_SRTSettings.EnemiesMaxDistance);
	if (!readSuccess)
		readSuccess = TryReadSetting(inputStringView, "EnemyHPBarsWidth=", g_SRTSettings.EnemyHPBarsWidth);
	if (!readSuccess)
		readSuccess = TryReadSetting(inputStringView, "EnemyHPBarsHeight=", g_SRTSettings.EnemyHPBarsHeight);
	if (!readSuccess)
		readSuccess = TryReadSetting(inputStringView, "EnemyHPBarForeColorIndex=", g_SRTSettings.EnemyHPBarForeColorIndex);
	if (!readSuccess)
		readSuccess = TryReadSetting(inputStringView, "EnemyHPBarBackColorIndex=", g_SRTSettings.EnemyHPBarBackColorIndex);
	if (!readSuccess)
		readSuccess = TryReadSetting(inputStringView, "DarkenBarColors=", g_SRTSettings.DarkenBarColors);

	if (!readSuccess)
		readSuccess = TryReadSetting(inputStringView, "DPIScalingFactor=", g_SRTSettings.DPIScalingFactor);
	if (!readSuccess)
		readSuccess = TryReadSetting(inputStringView, "FontScalingFactor=", g_SRTSettings.FontScalingFactor);

	// Debug settings, not shown in UI.
	if (!readSuccess)
		readSuccess = TryReadSetting(inputStringView, "DebugEnable=", g_SRTSettings.DebugEnable);
	if (!readSuccess)
		readSuccess = TryReadSetting(inputStringView, "DebugEnemiesShowNotSpawned=", g_SRTSettings.DebugEnemiesShowNotSpawned);
	if (!readSuccess)
		readSuccess = TryReadSetting(inputStringView, "DebugEnemiesShowPosition=", g_SRTSettings.DebugEnemiesShowPosition);
}

static void SRTSettings_WriteAll(ImGuiContext *, ImGuiSettingsHandler *handler, ImGuiTextBuffer *out_buf)
{
	out_buf->appendf("[%s][General]\n", handler->TypeName);

	out_buf->appendf("LogoPosition=%d\n", g_SRTSettings.LogoPosition);
	out_buf->appendf("LogoOpacity=%f\n", g_SRTSettings.LogoOpacity);

	out_buf->appendf("MainUIOpened=%d\n", g_SRTSettings.MainUIOpened);
	out_buf->appendf("MainOpacity=%f\n", g_SRTSettings.MainOpacity);

	out_buf->appendf("AboutUIOpened=%d\n", g_SRTSettings.AboutUIOpened);
	out_buf->appendf("AboutOpacity=%f\n", g_SRTSettings.AboutOpacity);

	out_buf->appendf("LogViewerOpened=%d\n", g_SRTSettings.LogViewerOpened);
	out_buf->appendf("LogViewerOpacity=%f\n", g_SRTSettings.LogViewerOpacity);
	out_buf->appendf("LogViewerAutoScroll=%d\n", g_SRTSettings.LogViewerAutoScroll);

	out_buf->appendf("OverlayGameInfoUIOpened=%d\n", g_SRTSettings.OverlayGameInfoUIOpened);
	out_buf->appendf("OverlayGameInfoOpacity=%f\n", g_SRTSettings.OverlayGameInfoOpacity);

	out_buf->appendf("OverlayPlayerUIOpened=%d\n", g_SRTSettings.OverlayPlayerUIOpened);
	out_buf->appendf("OverlayPlayerOpacity=%f\n", g_SRTSettings.OverlayPlayerOpacity);

	out_buf->appendf("OverlayInventoryUIOpened=%d\n", g_SRTSettings.OverlayInventoryUIOpened);
	out_buf->appendf("OverlayInventoryOpacity=%f\n", g_SRTSettings.OverlayInventoryOpacity);

	out_buf->appendf("OverlayEnemiesUIOpened=%d\n", g_SRTSettings.OverlayEnemiesUIOpened);
	out_buf->appendf("OverlayEnemiesOpacity=%f\n", g_SRTSettings.OverlayEnemiesOpacity);

	out_buf->appendf("ShowCustomizationOptions=%d\n", g_SRTSettings.ShowCustomizationOptions);
	out_buf->appendf("EnemiesShownLimit=%d\n", g_SRTSettings.EnemiesShownLimit);
	out_buf->appendf("EnemiesFullHPTextColorIndex=%d\n", g_SRTSettings.EnemiesFullHPTextColorIndex);
	out_buf->appendf("EnemiesInjuredTextColorIndex=%d\n", g_SRTSettings.EnemiesInjuredTextColorIndex);
	out_buf->appendf("EnemiesHideFullHP=%d\n", g_SRTSettings.EnemiesHideFullHP);
	out_buf->appendf("EnemyHPBarsVisible=%d\n", g_SRTSettings.EnemyHPBarsVisible);
	out_buf->appendf("EnemyHPBarsShowPercent=%d\n", g_SRTSettings.EnemyHPBarsShowPercent);
	out_buf->appendf("EnemiesMaxDistance=%f\n", g_SRTSettings.EnemiesMaxDistance);
	out_buf->appendf("EnemyHPBarsWidth=%f\n", g_SRTSettings.EnemyHPBarsWidth);
	out_buf->appendf("EnemyHPBarsHeight=%f\n", g_SRTSettings.EnemyHPBarsHeight);
	out_buf->appendf("EnemyHPBarForeColorIndex=%d\n", g_SRTSettings.EnemyHPBarForeColorIndex);
	out_buf->appendf("EnemyHPBarBackColorIndex=%d\n", g_SRTSettings.EnemyHPBarBackColorIndex);
	out_buf->appendf("DarkenBarColors=%d\n", g_SRTSettings.DarkenBarColors);

	out_buf->appendf("DPIScalingFactor=%f\n", g_SRTSettings.DPIScalingFactor);
	out_buf->appendf("FontScalingFactor=%f\n", g_SRTSettings.FontScalingFactor);

	// Debug settings, not shown in UI.
	out_buf->appendf("DebugEnable=%d\n", g_SRTSettings.DebugEnable);
	out_buf->appendf("DebugEnemiesShowNotSpawned=%d\n", g_SRTSettings.DebugEnemiesShowNotSpawned);
	out_buf->appendf("DebugEnemiesShowPosition=%d\n", g_SRTSettings.DebugEnemiesShowPosition);
}
