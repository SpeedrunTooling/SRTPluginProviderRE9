using ProcessMemory;
using System;
using System.Diagnostics;

namespace SRTPluginProviderRE9;

internal class GameMemoryScanner : IDisposable
{
    // Variables
    private ProcessMemoryHandler? memoryAccess;
    private GameMemory? gameMemoryValues;
    private GameVersion gameVersion;
    public bool HasScanned;
    public bool ProcessRunning => memoryAccess is not null && memoryAccess.ProcessRunning;
    public uint ProcessExitCode => (memoryAccess is not null) ? memoryAccess.ProcessExitCode : 0;

    // Pointer Address Variables
    private int pointerPropsManager;

    // Pointer Classes
    private IntPtr BaseAddress { get; set; }
    private MultilevelPointer PointerPlayerHP { get; set; }

    internal GameMemoryScanner(Process? process = null)
    {
        gameMemoryValues = new GameMemory();
        if (process is not null)
            Initialize(process);
    }

    internal unsafe void Initialize(Process process)
    {
        if (process is null)
            return; // Do not continue if this is null.

        int pid = process.Id;
        memoryAccess = new ProcessMemoryHandler((uint)pid);
        if (ProcessRunning)
        {
            BaseAddress = process?.MainModule?.BaseAddress ?? IntPtr.Zero; // Bypass .NET's managed solution for getting this and attempt to get this info ourselves via PInvoke since some users are getting 299 PARTIAL COPY when they seemingly shouldn't.
            gameVersion = GameHashes.DetectVersion(process.MainModule.FileName);
            SelectPointerAddresses(pid);
        }
    }

    private unsafe void SelectPointerAddresses(int pid)
    {
        switch (gameVersion)
        {
            case GameVersion.WW_20260225_1:
            default:
                {
                    pointerPropsManager = 0x00000000; // app.PropsManager
                    break;
                }
        }

        // Setup the pointers.
        PointerPlayerHP = new MultilevelPointer(
            memoryAccess,
            (nint*)IntPtr.Add(BaseAddress, pointerPropsManager),
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00
        );
    }

    internal void UpdatePointers()
    {
        PointerPlayerHP.UpdatePointers();
    }

    internal unsafe IGameMemory Refresh()
    {
        //Player HP
        //gameMemoryValues._player = PointerPlayerHP.Deref<GamePlayer>(0x00);

        HasScanned = true;
        return gameMemoryValues;
    }

    #region IDisposable Support
    private bool disposedValue = false;

    protected virtual void Dispose(bool disposing)
    {
        if (!disposedValue)
        {
            if (disposing)
            {
                memoryAccess?.Dispose();
            }

            disposedValue = true;
        }
    }

    public void Dispose() => Dispose(true);
    #endregion
}
