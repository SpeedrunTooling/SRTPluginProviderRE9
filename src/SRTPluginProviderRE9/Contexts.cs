using System;

namespace SRTPluginProviderRE9
{
    public abstract record class CharacterContext
    {
        public Guid ID { get; internal set; } // 0x38
        public ushort KindID { get; internal set; } // 0x40
        public HPData HP { get; internal set; } // 0x70
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
    }

    public record class EnemyContext : CharacterContext
    {
    }
}
