using System.Diagnostics;
using System.Reflection;

namespace SRTPluginProviderRE9;

internal class GameMemory : IGameMemory
{
    public string GameName => "RE9";
    public string VersionInfo => FileVersionInfo.GetVersionInfo(Assembly.GetExecutingAssembly().Location).FileVersion;

    public int DARank { get; set; }
    public int DAScore { get; set; }

    public HPData PlayerHP { get; set; }
    public HPData[] EnemyHPs { get; set; }
}
