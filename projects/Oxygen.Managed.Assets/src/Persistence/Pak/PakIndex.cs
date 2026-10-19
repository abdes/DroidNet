// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers.Binary;
using System.Text;
using Oxygen.Managed.Core;

namespace Oxygen.Managed.Assets.Persistence.Pak;

/// <summary>Reader for the browsable metadata of the current pak format: header, asset directory, browse index and catalog digest.</summary>
/// <remarks>The native <c>Oxygen/Data/PakFormat_core.h</c> is authoritative.</remarks>
public static class PakIndex
{
    public const int HeaderSize = 256;
    public const int FooterSize = 256;
    public const int DirectoryEntrySize = 64;
    public const int BrowseHeaderSize = 24;
    public const int BrowseEntrySize = 24;
    public const int BrowseTextureEntrySize = 12;
    public const ushort Version = 10;
    public const uint BrowseIndexVersion = 2;

    private const int FooterTextureTableOffset = 104;
    private const int FooterTextureTableCount = 112;
    private const int FooterTextureTableEntrySize = 116;
    private const int FooterBrowseOffset = 184;
    private const int FooterBrowseSize = 192;
    private const int FooterCatalogOffset = 200;
    private const int FooterCatalogSize = 208;
    private const int CatalogDigestSize = 32;
    private const int FooterMagicOffset = 248;

    private static readonly byte[] HeaderMagic = Encoding.ASCII.GetBytes("OXPAK\0\0\0");
    private static readonly byte[] FooterMagic = Encoding.ASCII.GetBytes("OXPAKEND");
    private static readonly byte[] BrowseMagic = Encoding.ASCII.GetBytes("OXPAKBIX");

    /// <summary>Reads and validates the browsable metadata of a pak file, sharing it with other readers only.</summary>
    /// <param name="path">The pak file.</param>
    /// <param name="cancellationToken">Cancels before the metadata is read.</param>
    /// <returns>The assets the pak's browse index names.</returns>
    /// <exception cref="IOException">The pak is missing, unreadable or being written.</exception>
    /// <exception cref="InvalidDataException">The pak is malformed, uses another format version, or has no browse index; repack it.</exception>
    public static async Task<PakDocument> ReadFileAsync(FileSystemPath path, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(path);
        var stream = new FileStream(path.FullPath, FileMode.Open, FileAccess.Read, FileShare.Read, 4096, FileOptions.Asynchronous);
        await using (stream.ConfigureAwait(false))
        {
            cancellationToken.ThrowIfCancellationRequested();
            return Read(stream);
        }
    }

    /// <summary>Reads and validates a pak's browsable metadata.</summary>
    /// <param name="stream">A seekable stream over the whole pak file.</param>
    /// <returns>The assets the pak's browse index names.</returns>
    /// <exception cref="InvalidDataException">The pak is malformed, uses another format version, or has no browse index; repack it.</exception>
    /// <exception cref="NotSupportedException">The stream is not seekable.</exception>
    public static PakDocument Read(Stream stream)
    {
        ArgumentNullException.ThrowIfNull(stream);
        if (!stream.CanSeek)
        {
            throw new NotSupportedException($"{nameof(PakIndex)}.{nameof(Read)} requires a seekable stream.");
        }

        var length = checked((ulong)stream.Length);
        if (length < HeaderSize + FooterSize)
        {
            throw new InvalidDataException("The pak is too small to contain a header and a footer.");
        }

        var header = ReadBlock(stream, 0, HeaderSize);
        var (contentVersion, sourceIdentity) = ReadHeader(header);
        var footer = ReadBlock(stream, length - FooterSize, FooterSize);
        if (!footer.AsSpan(FooterMagicOffset, 8).SequenceEqual(FooterMagic))
        {
            throw new InvalidDataException("Invalid pak footer magic.");
        }

        var directory = ReadDirectory(stream, footer, length - FooterSize);
        var textureCount = BinaryPrimitives.ReadUInt32LittleEndian(footer.AsSpan(FooterTextureTableCount, 4));
        var (assets, textures) = ReadBrowseIndex(stream, footer, length - FooterSize, directory, textureCount);
        return new PakDocument(contentVersion, sourceIdentity, ReadCatalogDigest(stream, footer, length - FooterSize), assets)
        {
            Textures = ReadTextures(stream, footer, length - FooterSize, textures),
        };
    }

    private static (ushort ContentVersion, Guid SourceIdentity) ReadHeader(byte[] header)
    {
        if (!header.AsSpan(0, 8).SequenceEqual(HeaderMagic))
        {
            throw new InvalidDataException("Invalid pak header magic.");
        }

        var version = BinaryPrimitives.ReadUInt16LittleEndian(header.AsSpan(8, 2));
        if (version != Version)
        {
            throw new InvalidDataException($"The pak uses format version {version}; this editor reads version {Version}. Repack the content.");
        }

        var sourceIdentity = new Guid(header.AsSpan(12, 16), bigEndian: true);
        if (sourceIdentity.Version != 7 || sourceIdentity.Variant is < 8 or > 11)
        {
            throw new InvalidDataException("The pak requires a UUIDv7 source identity.");
        }

        return (BinaryPrimitives.ReadUInt16LittleEndian(header.AsSpan(10, 2)), sourceIdentity);
    }

    private static Dictionary<AssetKey, DirectoryEntry> ReadDirectory(Stream stream, byte[] footer, ulong footerOffset)
    {
        var offset = BinaryPrimitives.ReadUInt64LittleEndian(footer.AsSpan(0, 8));
        var size = BinaryPrimitives.ReadUInt64LittleEndian(footer.AsSpan(8, 8));
        var count = BinaryPrimitives.ReadUInt64LittleEndian(footer.AsSpan(16, 8));
        if (count > size / DirectoryEntrySize || size != count * DirectoryEntrySize || offset > footerOffset || size > footerOffset - offset)
        {
            throw new InvalidDataException("The pak asset directory is out of bounds.");
        }

        var bytes = ReadBlock(stream, offset, checked((int)size));
        var directory = new Dictionary<AssetKey, DirectoryEntry>(checked((int)count));
        for (var index = 0; index < (int)count; index++)
        {
            var entry = bytes.AsSpan(index * DirectoryEntrySize, DirectoryEntrySize);
            var key = AssetKey.FromBytes(entry[..16]);
            var descriptorOffset = BinaryPrimitives.ReadUInt64LittleEndian(entry.Slice(25, 8));
            var descriptorSize = BinaryPrimitives.ReadUInt32LittleEndian(entry.Slice(33, 4));
            if (descriptorOffset > offset || descriptorSize > offset - descriptorOffset)
            {
                throw new InvalidDataException("A pak descriptor overlaps the asset directory.");
            }

            var references = new AssetReferenceTable(
                BinaryPrimitives.ReadUInt64LittleEndian(entry.Slice(37, 8)),
                BinaryPrimitives.ReadUInt32LittleEndian(entry.Slice(45, 4)),
                BinaryPrimitives.ReadUInt32LittleEndian(entry.Slice(49, 4)));
            if (!directory.TryAdd(key, new(entry[16], descriptorOffset, descriptorSize, references)))
            {
                throw new InvalidDataException($"The pak asset directory contains a duplicate key: {key}.");
            }
        }

        return directory;
    }

    private static List<PakTextureEntry> ReadTextures(Stream stream, byte[] footer, ulong footerOffset, List<(string Path, uint Index)> textures)
    {
        var offset = BinaryPrimitives.ReadUInt64LittleEndian(footer.AsSpan(FooterTextureTableOffset, 8));
        var count = BinaryPrimitives.ReadUInt32LittleEndian(footer.AsSpan(FooterTextureTableCount, 4));
        var entrySize = BinaryPrimitives.ReadUInt32LittleEndian(footer.AsSpan(FooterTextureTableEntrySize, 4));
        if (textures.Count == 0)
        {
            return [];
        }

        if (entrySize != TextureResourceRecord.Size || offset > footerOffset || (ulong)count * entrySize > footerOffset - offset)
        {
            throw new InvalidDataException("The pak texture table is out of bounds.");
        }

        return [.. textures.Select(texture => new PakTextureEntry(
            texture.Path,
            texture.Index,
            TextureResourceRecord.Read(ReadBlock(stream, offset + ((ulong)texture.Index * entrySize), TextureResourceRecord.Size))))];
    }

    private static (List<PakAssetEntry> Assets, List<(string Path, uint Index)> Textures) ReadBrowseIndex(
        Stream stream,
        byte[] footer,
        ulong footerOffset,
        Dictionary<AssetKey, DirectoryEntry> directory,
        uint textureCount)
    {
        var offset = BinaryPrimitives.ReadUInt64LittleEndian(footer.AsSpan(FooterBrowseOffset, 8));
        var size = BinaryPrimitives.ReadUInt64LittleEndian(footer.AsSpan(FooterBrowseSize, 8));
        if (offset == 0 || size == 0)
        {
            throw new InvalidDataException("The pak has no browse index; the editor lists pak assets by virtual path. Repack it with a browse index.");
        }

        if (offset > footerOffset || size > footerOffset - offset || size < BrowseHeaderSize)
        {
            throw new InvalidDataException("The pak browse index is out of bounds.");
        }

        var bytes = ReadBlock(stream, offset, checked((int)size));
        if (!bytes.AsSpan(0, 8).SequenceEqual(BrowseMagic))
        {
            throw new InvalidDataException("Invalid pak browse index magic.");
        }

        var version = BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(8, 4));
        if (version != BrowseIndexVersion)
        {
            throw new InvalidDataException($"The pak browse index uses version {version}; this editor reads version {BrowseIndexVersion}. Repack the content.");
        }

        var entryCount = BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(12, 4));
        var textureEntryCount = BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(16, 4));
        var stringTableSize = BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(20, 4));
        var entriesEnd = BrowseHeaderSize + ((ulong)entryCount * BrowseEntrySize) + ((ulong)textureEntryCount * BrowseTextureEntrySize);
        if (entriesEnd + stringTableSize > size)
        {
            throw new InvalidDataException("The pak browse index is truncated.");
        }

        var strings = bytes.AsSpan(checked((int)entriesEnd), checked((int)stringTableSize));
        var paths = new HashSet<string>(StringComparer.Ordinal);
        var assets = new List<PakAssetEntry>(checked((int)entryCount));
        for (var index = 0; index < (int)entryCount; index++)
        {
            var entry = bytes.AsSpan(BrowseHeaderSize + (index * BrowseEntrySize), BrowseEntrySize);
            var key = AssetKey.FromBytes(entry[..16]);
            var path = ReadPath(strings, entry.Slice(16, 8), paths);
            if (!directory.TryGetValue(key, out var listed))
            {
                throw new InvalidDataException($"The pak browse index names an asset its directory does not contain: {path}.");
            }

            assets.Add(new(key, listed.AssetType, path, listed.DescriptorOffset, listed.DescriptorSize) { References = listed.References });
        }

        var texturesStart = BrowseHeaderSize + ((int)entryCount * BrowseEntrySize);
        var textures = new List<(string Path, uint Index)>(checked((int)textureEntryCount));
        for (var index = 0; index < (int)textureEntryCount; index++)
        {
            var entry = bytes.AsSpan(texturesStart + (index * BrowseTextureEntrySize), BrowseTextureEntrySize);
            var textureIndex = BinaryPrimitives.ReadUInt32LittleEndian(entry[..4]);
            var path = ReadPath(strings, entry.Slice(4, 8), paths);
            if (textureIndex == 0 || textureIndex >= textureCount)
            {
                throw new InvalidDataException($"The pak browse index maps a texture outside the texture table: {path}.");
            }

            textures.Add((path, textureIndex));
        }

        return (assets, textures);
    }

    // The catalog ends with the SHA-256 digest of its canonical contents.
    private static string ReadCatalogDigest(Stream stream, byte[] footer, ulong footerOffset)
    {
        var offset = BinaryPrimitives.ReadUInt64LittleEndian(footer.AsSpan(FooterCatalogOffset, 8));
        var size = BinaryPrimitives.ReadUInt64LittleEndian(footer.AsSpan(FooterCatalogSize, 8));
        if (offset < HeaderSize || size < CatalogDigestSize || offset > footerOffset || size > footerOffset - offset)
        {
            throw new InvalidDataException("The pak catalog is missing or out of bounds.");
        }

        return Convert.ToHexString(ReadBlock(stream, offset + size - CatalogDigestSize, CatalogDigestSize));
    }

    private static string ReadPath(ReadOnlySpan<byte> strings, ReadOnlySpan<byte> slice, HashSet<string> paths)
    {
        var offset = BinaryPrimitives.ReadUInt32LittleEndian(slice[..4]);
        var length = BinaryPrimitives.ReadUInt32LittleEndian(slice.Slice(4, 4));
        if (length == 0 || offset > (uint)strings.Length || length > (uint)strings.Length - offset)
        {
            throw new InvalidDataException("A pak browse index path is outside its string table.");
        }

        var path = Encoding.UTF8.GetString(strings.Slice((int)offset, (int)length));
        if (path[0] != '/' || !paths.Add(path))
        {
            throw new InvalidDataException($"The pak browse index contains a relative or duplicate virtual path: {path}.");
        }

        return path;
    }

    private static byte[] ReadBlock(Stream stream, ulong offset, int size)
    {
        var bytes = new byte[size];
        stream.Position = checked((long)offset);
        stream.ReadExactly(bytes);
        return bytes;
    }

    private readonly record struct DirectoryEntry(byte AssetType, ulong DescriptorOffset, uint DescriptorSize, AssetReferenceTable References);
}
