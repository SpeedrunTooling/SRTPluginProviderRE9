using System;
using System.IO;
using System.Linq;
using System.Security.Cryptography;

namespace SRTPluginProviderRE9;

/// <summary>
/// SHA256 hashes for the RE9/BIO9 game executables.
/// 
/// Resident Evil 9: Requiem (WW): https://steamdb.info/app/3764200/ / https://steamdb.info/depot/3764201/
/// Biohazard 9: Requiem (CERO Z): https://steamdb.info/app/3990820/ or 3990800?
/// </summary>
public static class GameHashes
{
    private static readonly byte[] ww_20260225_1 = [0x44, 0x04, 0x4E, 0xCF, 0xB7, 0x15, 0x28, 0x26, 0x32, 0x05, 0x86, 0xE5, 0xCC, 0x40, 0xFE, 0x8C, 0xEE, 0xB2, 0x77, 0x2C, 0x58, 0x07, 0x98, 0x51, 0x13, 0xA5, 0x90, 0x4B, 0xFD, 0xF3, 0x58, 0xC7];

    public static GameVersion DetectVersion(string filePath)
    {
        byte[] checksum;
        using (SHA256 hashFunc = SHA256.Create())
        using (FileStream fs = new FileStream(filePath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
            checksum = hashFunc.ComputeHash(fs);

        if (checksum.SequenceEqual(ww_20260225_1))
        {
            Console.WriteLine("2026-02-25 version detected.");
            return GameVersion.WW_20260225_1;
        }

        Console.WriteLine("Unknown Version. If you have installed any third party mods, please uninstall and try again.");
        return GameVersion.Unknown;
    }
}
