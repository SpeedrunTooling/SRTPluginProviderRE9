using System;
using System.IO;
using System.Linq;
using System.Security.Cryptography;

namespace SRTPluginProviderRE9;

/// <summary>
/// SHA256 hashes for the RE9 game executables.
/// 
/// Resident Evil 9: Requiem (WW): https://steamdb.info/app/3764200/ / https://steamdb.info/depot/3764201/
/// </summary>
public static class GameHashes
{
    // AppID 3764200 DepotID 3764201 ManifestIDs 2990234554016452841
    private static readonly byte[] ww_20260327_1 = [0xA0, 0x12, 0x51, 0x45, 0x3A, 0x8F, 0x08, 0x63, 0x90, 0x6C, 0x04, 0xE0, 0x86, 0x7A, 0x92, 0x11, 0x69, 0x63, 0x9F, 0xC8, 0xA1, 0xA4, 0xD7, 0x73, 0x6D, 0x73, 0xBA, 0x7C, 0xA4, 0x91, 0x46, 0x82];
    private static readonly byte[] ww_20260313_1 = [0x6E, 0xD6, 0xB8, 0x46, 0xA8, 0xAE, 0x92, 0xBC, 0x9F, 0xF3, 0x7C, 0xBB, 0x82, 0x6E, 0xED, 0x1C, 0x2A, 0x1B, 0x53, 0x3C, 0xE3, 0x8B, 0x2F, 0x1D, 0x82, 0xF7, 0xDB, 0x27, 0x1B, 0xAF, 0xF3, 0xD8];
    private static readonly byte[] ww_20260305_1 = [0x3B, 0xBF, 0x9D, 0x54, 0x26, 0xA5, 0x03, 0xE8, 0x82, 0xEA, 0x7B, 0xD6, 0xBA, 0x4E, 0x5E, 0xB3, 0x20, 0xDE, 0x38, 0xF7, 0x09, 0x36, 0x49, 0xD9, 0xAA, 0x61, 0x6A, 0xD0, 0xF0, 0x48, 0xAD, 0x06];
    private static readonly byte[] ww_20260225_1 = [0x44, 0x04, 0x4E, 0xCF, 0xB7, 0x15, 0x28, 0x26, 0x32, 0x05, 0x86, 0xE5, 0xCC, 0x40, 0xFE, 0x8C, 0xEE, 0xB2, 0x77, 0x2C, 0x58, 0x07, 0x98, 0x51, 0x13, 0xA5, 0x90, 0x4B, 0xFD, 0xF3, 0x58, 0xC7];

    public static GameVersion DetectVersion(string filePath)
    {
        byte[] checksum;
        using (SHA256 hashFunc = SHA256.Create())
        using (FileStream fs = new FileStream(filePath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
            checksum = hashFunc.ComputeHash(fs);

        if (checksum.SequenceEqual(ww_20260327_1))
        {
            Console.WriteLine("2026-03-27 version detected.");
            return GameVersion.WW_20260327_1;
        }
        else if (checksum.SequenceEqual(ww_20260313_1))
        {
            Console.WriteLine("2026-03-13 version detected.");
            return GameVersion.WW_20260313_1;
        }
        else if (checksum.SequenceEqual(ww_20260305_1))
        {
            Console.WriteLine("2026-03-05 version detected.");
            return GameVersion.WW_20260305_1;
        }
        else if (checksum.SequenceEqual(ww_20260225_1))
        {
            Console.WriteLine("2026-02-25 version detected.");
            return GameVersion.WW_20260225_1;
        }

        Console.WriteLine("Unknown Version. If you have installed any third party mods, please uninstall and try again.");
        return GameVersion.Unknown;
    }
}
