using SRTPluginBase;
using System;

namespace SRTPluginProviderRE9;

internal class PluginInfo : IPluginInfo
{
    public string Name => "Game Memory Provider (Resident Evil 9: Requiem (2026))";

    public string Description => "A game memory provider plugin for Resident Evil 9: Requiem (2026).";

    public string Author => "Squirrelies";

    public Uri MoreInfoURL => new Uri("https://github.com/SpeedrunTooling/SRTPluginProviderRE9");

    public int VersionMajor => assemblyVersion.Major;

    public int VersionMinor => assemblyVersion.Minor;

    public int VersionBuild => assemblyVersion.Build;

    public int VersionRevision => assemblyVersion.Revision;

    private Version assemblyVersion = System.Reflection.Assembly.GetExecutingAssembly().GetName().Version;
}
