using System.Diagnostics;
using System.Reflection;

namespace SRTPluginProviderRE9;

public class GameMemory : IGameMemory
{
    public string GameName => "RE9";
    public string VersionInfo => FileVersionInfo.GetVersionInfo(Assembly.GetExecutingAssembly().Location).FileVersion;

}
