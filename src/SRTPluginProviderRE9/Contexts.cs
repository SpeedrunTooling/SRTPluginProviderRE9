using System;
using ProcessMemory;
using SRTPluginProviderRE9.Extensions;

namespace SRTPluginProviderRE9
{
    public abstract unsafe record class CharacterContext
    {
        public CharacterContext(ProcessMemoryHandler processMemoryHandler, MultilevelPointer multilevelPointer)
        {
            var contextIdPtr = processMemoryHandler.DerefChain<nuint>(multilevelPointer.Address, 0x38);
            ID = processMemoryHandler.GetAt<Guid>(contextIdPtr + 0x10U);

            var kindIdPtr = processMemoryHandler.DerefChain<nuint>(multilevelPointer.Address, 0x40, 0x10);
            var kindIdLength = processMemoryHandler.GetAt<int>(kindIdPtr + 0x10U) * sizeof(char);
            KindID = processMemoryHandler.GetUnicodeStringAt(UIntPtr.Add(kindIdPtr, 0x14).ToPointer(), (nuint)kindIdLength);

            HP = new HPData
            {
                CurrentHP = processMemoryHandler.DerefChain<int>(multilevelPointer.Address, 0x70, 0x10, 0x28),
                CurrentMaxHP = processMemoryHandler.DerefChain<int>(multilevelPointer.Address, 0x70, 0x10, 0x30)
            };
            IsRespawn = multilevelPointer.DerefByte(0xEC) != 0;
            IsSpawn = multilevelPointer.DerefByte(0xED) != 0;
            StopByOutOfArea = multilevelPointer.DerefByte(0xEE) != 0;
            IsSuspended = multilevelPointer.DerefByte(0xEF) != 0;
            CutSceneInvalidated = multilevelPointer.DerefByte(0xF0) != 0;
            Managed = multilevelPointer.DerefByte(0xF1) != 0;
            ShowVitalSkipFrame = multilevelPointer.DerefByte(0xF2) != 0;
        }

        public Guid ID { get; internal set; } // 0x38 -> 0x16
        public string KindID { get; internal set; } // 0x40 -> 0x10 -> 0x14 + N
        public HPData HP { get; internal set; } // 0x70 -> 0x10 -> (0x28, 0x30)
        public bool IsRespawn { get; internal set; } // 0xEC
        public bool IsSpawn { get; internal set; } // 0xED
        public bool StopByOutOfArea { get; internal set; } // 0xEE
        public bool IsSuspended { get; internal set; } // 0xEF
        public bool CutSceneInvalidated { get; internal set; } // 0xF0
        public bool Managed { get; internal set; } // 0xF1
        public bool ShowVitalSkipFrame { get; internal set; } // 0xF2

    }

    public record class PlayerContext : CharacterContext
    {
        public PlayerContext(ProcessMemoryHandler processMemoryHandler, MultilevelPointer multilevelPointer) : base(processMemoryHandler, multilevelPointer)
        {
        }
    }

    public record class EnemyContext : CharacterContext
    {
        public EnemyContext(ProcessMemoryHandler processMemoryHandler, MultilevelPointer multilevelPointer) : base(processMemoryHandler, multilevelPointer)
        {
        }
    }
}
