// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers.Binary;
using System.Text;
using Oxygen.Managed.Assets.Persistence;
using Oxygen.Managed.Assets.Persistence.Pak;

namespace Oxygen.Managed.Assets.Testing;

/// <summary>Small pak emitter for reader tests: header, zeroed descriptors, directory, browse index, a catalog ending with the document's digest and footer; never shipped.</summary>
public static class PakFixture
{
    /// <summary>Writes a pak holding <paramref name="document"/>'s assets; their descriptor offsets are laid out in order.</summary>
    /// <param name="destination">The stream to replace with the pak.</param>
    /// <param name="document">The header fields and the assets, listed in the directory and the browse index.</param>
    /// <param name="textures">The texture entries of the browse index.</param>
    /// <param name="textureTableCount">The entries of the texture table, which the footer records.</param>
    /// <param name="textureRecords">The leading texture table records; the others are zero.</param>
    /// <param name="browseIndex">Whether the pak embeds a browse index.</param>
    /// <param name="browseOnly">Assets listed in the browse index but missing from the directory.</param>
    public static void Write(
        Stream destination,
        PakDocument document,
        IReadOnlyList<(uint TextureIndex, string VirtualPath)>? textures = null,
        uint textureTableCount = 0,
        bool browseIndex = true,
        IReadOnlyList<PakAssetEntry>? browseOnly = null,
        IReadOnlyList<TextureResourceRecord>? textureRecords = null)
    {
        ArgumentNullException.ThrowIfNull(destination);
        ArgumentNullException.ThrowIfNull(document);
        textures ??= [];
        textureRecords ??= [];
        browseOnly ??= [];
        destination.SetLength(0);
        var header = new byte[PakIndex.HeaderSize];
        "OXPAK\0\0\0"u8.CopyTo(header);
        BinaryPrimitives.WriteUInt16LittleEndian(header.AsSpan(8), PakIndex.Version);
        BinaryPrimitives.WriteUInt16LittleEndian(header.AsSpan(10), document.ContentVersion);
        document.SourceIdentity.ToByteArray(bigEndian: true).CopyTo(header, 12);
        destination.Write(header);

        var offsets = new List<ulong>();
        foreach (var asset in document.Assets)
        {
            offsets.Add(checked((ulong)destination.Position));
            destination.Write(new byte[asset.DescriptorSize]);
        }

        var textureTableOffset = checked((ulong)destination.Position);
        for (var index = 0; index < textureTableCount; index++)
        {
            destination.Write(index < textureRecords.Count ? Encode(textureRecords[index]) : new byte[TextureResourceRecord.Size]);
        }

        var directoryOffset = checked((ulong)destination.Position);
        for (var index = 0; index < document.Assets.Count; index++)
        {
            var asset = document.Assets[index];
            var entry = new byte[PakIndex.DirectoryEntrySize];
            asset.AssetKey.WriteBytes(entry);
            entry[16] = asset.AssetType;
            BinaryPrimitives.WriteUInt64LittleEndian(entry.AsSpan(17), directoryOffset + ((ulong)index * PakIndex.DirectoryEntrySize));
            BinaryPrimitives.WriteUInt64LittleEndian(entry.AsSpan(25), offsets[index]);
            BinaryPrimitives.WriteUInt32LittleEndian(entry.AsSpan(33), asset.DescriptorSize);
            BinaryPrimitives.WriteUInt64LittleEndian(entry.AsSpan(37), asset.References.Offset);
            BinaryPrimitives.WriteUInt32LittleEndian(entry.AsSpan(45), asset.References.ResourceCount);
            BinaryPrimitives.WriteUInt32LittleEndian(entry.AsSpan(49), asset.References.KeyCount);
            destination.Write(entry);
        }

        var directorySize = checked((ulong)destination.Position) - directoryOffset;
        var browseOffset = browseIndex ? checked((ulong)destination.Position) : 0;
        if (browseIndex)
        {
            destination.Write(EncodeBrowseIndex([.. document.Assets, .. browseOnly], textures));
        }

        var browseSize = browseIndex ? checked((ulong)destination.Position) - browseOffset : 0;
        var catalogOffset = checked((ulong)destination.Position);
        destination.Write(new byte[8]);
        destination.Write(Convert.FromHexString(document.CatalogDigest));
        var catalogSize = checked((ulong)destination.Position) - catalogOffset;
        var footer = new byte[PakIndex.FooterSize];
        BinaryPrimitives.WriteUInt64LittleEndian(footer.AsSpan(0), directoryOffset);
        BinaryPrimitives.WriteUInt64LittleEndian(footer.AsSpan(8), directorySize);
        BinaryPrimitives.WriteUInt64LittleEndian(footer.AsSpan(16), (ulong)document.Assets.Count);
        BinaryPrimitives.WriteUInt64LittleEndian(footer.AsSpan(104), textureTableOffset);
        BinaryPrimitives.WriteUInt32LittleEndian(footer.AsSpan(112), textureTableCount);
        BinaryPrimitives.WriteUInt32LittleEndian(footer.AsSpan(116), TextureResourceRecord.Size);
        BinaryPrimitives.WriteUInt64LittleEndian(footer.AsSpan(184), browseOffset);
        BinaryPrimitives.WriteUInt64LittleEndian(footer.AsSpan(192), browseSize);
        BinaryPrimitives.WriteUInt64LittleEndian(footer.AsSpan(200), catalogOffset);
        BinaryPrimitives.WriteUInt64LittleEndian(footer.AsSpan(208), catalogSize);
        "OXPAKEND"u8.CopyTo(footer.AsSpan(248));
        destination.Write(footer);
    }

    /// <summary>Encodes a texture table record as the engine lays it out.</summary>
    /// <param name="record">The record.</param>
    /// <returns>The record's bytes.</returns>
    public static byte[] Encode(TextureResourceRecord record)
    {
        ArgumentNullException.ThrowIfNull(record);
        var bytes = new byte[TextureResourceRecord.Size];
        bytes[12] = record.TextureType;
        BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(14), record.Width);
        BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(18), record.Height);
        BinaryPrimitives.WriteUInt16LittleEndian(bytes.AsSpan(22), 1);
        BinaryPrimitives.WriteUInt16LittleEndian(bytes.AsSpan(24), record.ArrayLayers);
        bytes[28] = record.Format;
        return bytes;
    }

    private static byte[] EncodeBrowseIndex(
        IReadOnlyList<PakAssetEntry> assets,
        IReadOnlyList<(uint TextureIndex, string VirtualPath)> textures)
    {
        using var strings = new MemoryStream();
        (uint Offset, uint Length) Add(string path)
        {
            var bytes = Encoding.UTF8.GetBytes(path);
            var offset = checked((uint)strings.Length);
            strings.Write(bytes);
            return (offset, checked((uint)bytes.Length));
        }

        using var payload = new MemoryStream();
        var header = new byte[PakIndex.BrowseHeaderSize];
        "OXPAKBIX"u8.CopyTo(header);
        BinaryPrimitives.WriteUInt32LittleEndian(header.AsSpan(8), PakIndex.BrowseIndexVersion);
        BinaryPrimitives.WriteUInt32LittleEndian(header.AsSpan(12), checked((uint)assets.Count));
        BinaryPrimitives.WriteUInt32LittleEndian(header.AsSpan(16), checked((uint)textures.Count));
        using var entries = new MemoryStream();
        foreach (var asset in assets)
        {
            var entry = new byte[PakIndex.BrowseEntrySize];
            asset.AssetKey.WriteBytes(entry);
            var (offset, length) = Add(asset.VirtualPath);
            BinaryPrimitives.WriteUInt32LittleEndian(entry.AsSpan(16), offset);
            BinaryPrimitives.WriteUInt32LittleEndian(entry.AsSpan(20), length);
            entries.Write(entry);
        }

        foreach (var (textureIndex, virtualPath) in textures)
        {
            var entry = new byte[PakIndex.BrowseTextureEntrySize];
            BinaryPrimitives.WriteUInt32LittleEndian(entry, textureIndex);
            var (offset, length) = Add(virtualPath);
            BinaryPrimitives.WriteUInt32LittleEndian(entry.AsSpan(4), offset);
            BinaryPrimitives.WriteUInt32LittleEndian(entry.AsSpan(8), length);
            entries.Write(entry);
        }

        BinaryPrimitives.WriteUInt32LittleEndian(header.AsSpan(20), checked((uint)strings.Length));
        payload.Write(header);
        payload.Write(entries.ToArray());
        payload.Write(strings.ToArray());
        return payload.ToArray();
    }
}
