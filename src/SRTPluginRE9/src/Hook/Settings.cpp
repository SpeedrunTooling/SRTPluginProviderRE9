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
		readSuccess = TryReadSetting(inputStringView, "MainOpacity=", g_SRTSettings.MainOpacity);

	if (!readSuccess)
		readSuccess = TryReadSetting(inputStringView, "AboutOpacity=", g_SRTSettings.AboutOpacity);

	if (!readSuccess)
		readSuccess = TryReadSetting(inputStringView, "LoggerOpacity=", g_SRTSettings.LoggerOpacity);
	if (!readSuccess)
		readSuccess = TryReadSetting(inputStringView, "LoggerAutoScroll=", g_SRTSettings.LoggerAutoScroll);

	if (!readSuccess)
		readSuccess = TryReadSetting(inputStringView, "OverlayOpacity=", g_SRTSettings.OverlayOpacity);

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
}

static void SRTSettings_WriteAll(ImGuiContext *, ImGuiSettingsHandler *handler, ImGuiTextBuffer *out_buf)
{
	out_buf->appendf("[%s][General]\n", handler->TypeName);

	out_buf->appendf("LogoPosition=%d\n", g_SRTSettings.LogoPosition);
	out_buf->appendf("LogoOpacity=%f\n", g_SRTSettings.LogoOpacity);

	out_buf->appendf("MainOpacity=%f\n", g_SRTSettings.MainOpacity);

	out_buf->appendf("AboutOpacity=%f\n", g_SRTSettings.AboutOpacity);

	out_buf->appendf("LoggerOpacity=%f\n", g_SRTSettings.LoggerOpacity);
	out_buf->appendf("LoggerAutoScroll=%d\n", g_SRTSettings.LoggerAutoScroll);

	out_buf->appendf("OverlayOpacity=%f\n", g_SRTSettings.OverlayOpacity);

	out_buf->appendf("ShowCustomizationOptions=%d\n", g_SRTSettings.ShowCustomizationOptions);
	out_buf->appendf("EnemiesShownLimit=%d\n", g_SRTSettings.EnemiesShownLimit);
	out_buf->appendf("EnemiesFullHPTextColorIndex=%d\n", g_SRTSettings.EnemiesFullHPTextColorIndex);
	out_buf->appendf("EnemiesInjuredTextColorIndex=%d\n", g_SRTSettings.EnemiesInjuredTextColorIndex);
	out_buf->appendf("EnemiesHideFullHP=%d\n", g_SRTSettings.EnemiesHideFullHP);
	out_buf->appendf("EnemyHPBarsVisible=%d\n", g_SRTSettings.EnemyHPBarsVisible);
	out_buf->appendf("EnemyHPBarsShowPercent=%d\n", g_SRTSettings.EnemyHPBarsShowPercent);
	out_buf->appendf("EnemyHPBarsWidth=%f\n", g_SRTSettings.EnemyHPBarsWidth);
	out_buf->appendf("EnemyHPBarsHeight=%f\n", g_SRTSettings.EnemyHPBarsHeight);
	out_buf->appendf("EnemyHPBarForeColorIndex=%d\n", g_SRTSettings.EnemyHPBarForeColorIndex);
	out_buf->appendf("EnemyHPBarBackColorIndex=%d\n", g_SRTSettings.EnemyHPBarBackColorIndex);
	out_buf->appendf("DarkenBarColors=%d\n", g_SRTSettings.DarkenBarColors);

	out_buf->appendf("DPIScalingFactor=%f\n", g_SRTSettings.DPIScalingFactor);
	out_buf->appendf("FontScalingFactor=%f\n", g_SRTSettings.FontScalingFactor);
}
