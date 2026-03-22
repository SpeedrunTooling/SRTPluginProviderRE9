using System;
using System.Diagnostics;
using System.Linq;
using System.Net;
using System.Runtime.CompilerServices;
using ProcessMemory;

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
    private int pointerAppRankManager;
    private int pointerAppCharacterManager;

    // Pointer Classes
    private IntPtr BaseAddress { get; set; }
    private MultilevelPointer PointerRankProfile { get; set; }
    private MultilevelPointer PointerPlayerContext { get; set; }
    private MultilevelPointer PointerEnemyContextList { get; set; }
    private MultilevelPointer[] PointerEnemyContexts { get; set; }


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
                    pointerAppRankManager = 0x0E815400; // app.RankManager
                    pointerAppCharacterManager = 0x0E843CF8; // app.CharacterManager
                    break;
                }
        }

        // Setup the pointers.
        PointerRankProfile = new MultilevelPointer(
            memoryAccess,
            (nint*)IntPtr.Add(BaseAddress, pointerAppRankManager),
            0x30
        );

        PointerPlayerContext = new MultilevelPointer(
            memoryAccess,
            (nint*)IntPtr.Add(BaseAddress, pointerAppCharacterManager),
            0xB0
        );

        PointerEnemyContextList = new MultilevelPointer(
            memoryAccess,
            (nint*)IntPtr.Add(BaseAddress, pointerAppCharacterManager),
            0xB8
        );

        RefreshEnemyContexts(PointerEnemyContextList.DerefInt(0x18));
    }

    private unsafe void RefreshEnemyContexts(int enemyContextCount)
    {
        PointerEnemyContexts = new MultilevelPointer[enemyContextCount];
        gameMemoryValues.EnemyContexts = new EnemyContext[enemyContextCount];
        for (var i = 0; i < PointerEnemyContexts.Length; ++i)
        {
            PointerEnemyContexts[i] = new MultilevelPointer(
                memoryAccess,
                (nint*)IntPtr.Add(BaseAddress, pointerAppCharacterManager),
                0xB8,
                0x10,
                0x20 + (i * 0x8)
            );
        }
    }

    public unsafe T DerefChain<T>(IntPtr baseAddress, params int[] offsets) where T : unmanaged
    {
        for (var i = 0; i < offsets.Length - 1; ++i)
            baseAddress = memoryAccess.GetNIntAt(IntPtr.Add(baseAddress, offsets[i]).ToPointer());
        return memoryAccess.GetAt<T>(IntPtr.Add(baseAddress, offsets[offsets.Length - 1]).ToPointer());
    }

    internal void UpdatePointers()
    {
        PointerRankProfile.UpdatePointers();
        PointerPlayerContext.UpdatePointers();
        PointerEnemyContextList.UpdatePointers();
        var enemyContextCount = PointerEnemyContextList.DerefInt(0x18);
        if (enemyContextCount != PointerEnemyContexts.Length)
            RefreshEnemyContexts(enemyContextCount);
        else
            for (var i = 0; i < PointerEnemyContexts.Length; ++i)
                PointerEnemyContexts[i].UpdatePointers();
    }

    internal unsafe IGameMemory Refresh()
    {
        // DA
        gameMemoryValues.DAScore = PointerRankProfile.DerefInt(0x14);
        gameMemoryValues.DARank = PointerRankProfile.DerefInt(0x18);

        // Player HP
        gameMemoryValues.PlayerContext = new PlayerContext
        {
            ID = PointerPlayerContext.Deref<Guid>(0x38),
            HP = new HPData
            {
                CurrentHP = DerefChain<int>(PointerPlayerContext.Address, 0x70, 0x10, 0x28),
                CurrentMaxHP = DerefChain<int>(PointerPlayerContext.Address, 0x70, 0x10, 0x30)
            },
            IsRespawn = PointerPlayerContext.DerefByte(0xEC) != 0,
            IsSpawn = PointerPlayerContext.DerefByte(0xED) != 0,
            StopByOutOfArea = PointerPlayerContext.DerefByte(0xEE) != 0,
            IsSuspended = PointerPlayerContext.DerefByte(0xEF) != 0,
            CutSceneInvalidated = PointerPlayerContext.DerefByte(0xF0) != 0,
            Managed = PointerPlayerContext.DerefByte(0xF1) != 0,
            ShowVitalSkipFrame = PointerPlayerContext.DerefByte(0xF2) != 0,
        };

        // Enemy HPs
        for (var i = 0; i < PointerEnemyContexts.Length; ++i)
        {
            gameMemoryValues.EnemyContexts[i] = new EnemyContext
            {
                ID = PointerEnemyContexts[i].Deref<Guid>(0x38),
                KindID = PointerEnemyContexts[i].DerefUShort(0x40),
                HP = new HPData
                {
                    CurrentHP = DerefChain<int>(PointerEnemyContexts[i].Address, 0x70, 0x10, 0x28),
                    CurrentMaxHP = DerefChain<int>(PointerEnemyContexts[i].Address, 0x70, 0x10, 0x30)
                },
                IsRespawn = PointerEnemyContexts[i].DerefByte(0xEC) != 0,
                IsSpawn = PointerEnemyContexts[i].DerefByte(0xED) != 0,
                StopByOutOfArea = PointerEnemyContexts[i].DerefByte(0xEE) != 0,
                IsSuspended = PointerEnemyContexts[i].DerefByte(0xEF) != 0,
                CutSceneInvalidated = PointerEnemyContexts[i].DerefByte(0xF0) != 0,
                Managed = PointerEnemyContexts[i].DerefByte(0xF1) != 0,
                ShowVitalSkipFrame = PointerEnemyContexts[i].DerefByte(0xF2) != 0,
            };
        }

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
