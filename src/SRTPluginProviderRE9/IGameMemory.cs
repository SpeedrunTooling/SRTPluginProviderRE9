namespace SRTPluginProviderRE9;

public interface IGameMemory
{
    string GameName { get; }
    string VersionInfo { get; }

    int DARank { get; }
    int DAScore { get; }

    HPData PlayerHP { get; }
    HPData[] EnemyHPs { get; }
}
