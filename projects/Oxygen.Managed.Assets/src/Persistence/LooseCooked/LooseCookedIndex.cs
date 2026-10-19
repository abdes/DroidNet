// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers;
using System.Buffers.Binary;
using System.Text;

namespace Oxygen.Managed.Assets.Persistence.LooseCooked;

/// <summary>Reader for the current <c>container.index.bin</c> format.</summary>
/// <remarks>The native <c>Oxygen/Data/LooseCookedIndexFormat.h</c> is authoritative.</remarks>
public static class LooseCookedIndex
{
    public const int HeaderSize = 256;
    public const int AssetEntrySize = 85;
    public const int FileRecordSize = 54;
    public const int Sha256Size = 32;
    public const ushort Version = 6;

    private static readonly byte[] HeaderMagic = Encoding.ASCII.GetBytes("OXLCIDX\0");

    /// <summary>
    /// Reads the format version an index declares, without validating anything else, so that content cooked in another
    /// format can be told apart from damaged content.
    /// </summary>
    /// <param name="bytes">The index's leading bytes.</param>
    /// <returns>The declared version, or null when the bytes do not start with a loose index header.</returns>
    public static ushort? ReadFormatVersion(ReadOnlySpan<byte> bytes)
        => bytes.Length >= HeaderSize && bytes[..8].SequenceEqual(HeaderMagic)
            ? BinaryPrimitives.ReadUInt16LittleEndian(bytes.Slice(8, 2))
            : null;

    /// <summary>Reads and validates current loose-index metadata.</summary>
    /// <param name="stream">A seekable stream positioned anywhere in the index.</param>
    /// <returns>The index metadata.</returns>
    /// <exception cref="InvalidDataException">The index is malformed or uses another format version; recook it.</exception>
    /// <exception cref="NotSupportedException">The stream is not seekable.</exception>
    public static Document Read(Stream stream)
    {
        ArgumentNullException.ThrowIfNull(stream);
        EnsureSeekable(stream);
        var length = checked((ulong)stream.Length);
        if (length < HeaderSize)
        {
            throw new InvalidDataException("Index is too small to contain a header.");
        }

        var header = ReadHeader(stream);
        ValidateHeader(header, length);
        var stringTable = ReadBlock(stream, header.StringTableOffset, checked((int)header.StringTableSize));
        var assets = ReadAssets(stream, header, stringTable);
        var files = header.FileRecordCount == 0 ? [] : ReadFiles(stream, header, stringTable);
        var document = new Document(header.ContentVersion, header.Flags, header.SourceGuid, assets, files);
        ValidateDocument(document);
        return document;
    }

    private static void EnsureSeekable(Stream stream)
    {
        if (!stream.CanSeek)
        {
            throw new NotSupportedException($"{nameof(LooseCookedIndex)}.{nameof(Read)} requires a seekable stream.");
        }
    }

    private static HeaderFields ReadHeader(Stream stream)
    {
        Span<byte> bytes = stackalloc byte[HeaderSize];
        stream.Position = 0;
        stream.ReadExactly(bytes);
        if (!bytes[..8].SequenceEqual(HeaderMagic))
        {
            throw new InvalidDataException("Invalid LooseCookedIndex header magic.");
        }

        var version = BinaryPrimitives.ReadUInt16LittleEndian(bytes.Slice(8, 2));
        if (version != Version)
        {
            throw new InvalidDataException($"The cooked index uses format version {version}; this editor reads version {Version}. Recook the content.");
        }

        var flagsRaw = BinaryPrimitives.ReadUInt32LittleEndian(bytes.Slice(12, 4));
        var flags = (IndexFeatures)flagsRaw;
        var knownMask = (uint)(IndexFeatures.HasVirtualPaths | IndexFeatures.HasFileRecords);
        if ((flagsRaw & ~knownMask) != 0)
        {
            throw new InvalidDataException("Index flags contain unknown bits.");
        }

        return new(
            BinaryPrimitives.ReadUInt16LittleEndian(bytes.Slice(10, 2)),
            flags,
            ReadCanonicalGuid(bytes.Slice(16, 16)),
            BinaryPrimitives.ReadUInt64LittleEndian(bytes.Slice(32, 8)),
            BinaryPrimitives.ReadUInt64LittleEndian(bytes.Slice(40, 8)),
            BinaryPrimitives.ReadUInt64LittleEndian(bytes.Slice(48, 8)),
            BinaryPrimitives.ReadUInt32LittleEndian(bytes.Slice(56, 4)),
            BinaryPrimitives.ReadUInt32LittleEndian(bytes.Slice(60, 4)),
            BinaryPrimitives.ReadUInt64LittleEndian(bytes.Slice(64, 8)),
            BinaryPrimitives.ReadUInt32LittleEndian(bytes.Slice(72, 4)),
            BinaryPrimitives.ReadUInt32LittleEndian(bytes.Slice(76, 4)));
    }

    private static void ValidateHeader(HeaderFields header, ulong length)
    {
        if (header.SourceGuid.Version != 7 || header.SourceGuid.Variant is < 8 or > 11)
        {
            throw new InvalidDataException("The cooked index requires a UUIDv7 source identity.");
        }

        const IndexFeatures known = IndexFeatures.HasVirtualPaths | IndexFeatures.HasFileRecords;
        if ((header.Flags & ~known) != 0 || (header.Flags & IndexFeatures.HasVirtualPaths) == 0
            || (header.FileRecordCount > 0 && (header.Flags & IndexFeatures.HasFileRecords) == 0))
        {
            throw new InvalidDataException("The cooked index has invalid section flags.");
        }

        ValidateRange(header.StringTableOffset, header.StringTableSize, length, "string table");
        if (header.AssetEntrySize != AssetEntrySize)
        {
            throw new InvalidDataException($"Asset entry size {header.AssetEntrySize} does not match the expected size {AssetEntrySize}.");
        }

        checked
        {
            var assetsSize = (ulong)header.AssetCount * header.AssetEntrySize;
            ValidateRange(header.AssetEntriesOffset, assetsSize, length, "asset entries");
            if (header.FileRecordsOffset < header.AssetEntriesOffset + assetsSize)
            {
                throw new InvalidDataException("File records must start after the end of the asset entries.");
            }
        }

        if (header.FileRecordCount > 0)
        {
            if (header.FileRecordSize != FileRecordSize)
            {
                throw new InvalidDataException($"File record size {header.FileRecordSize} does not match the expected size {FileRecordSize}.");
            }

            checked
            {
                ValidateRange(header.FileRecordsOffset, (ulong)header.FileRecordCount * header.FileRecordSize, length, "file records");
            }
        }
    }

    private static List<AssetEntry> ReadAssets(Stream stream, HeaderFields header, byte[] stringTable)
    {
        var assets = new List<AssetEntry>(checked((int)header.AssetCount));
        stream.Position = checked((long)header.AssetEntriesOffset);
        var buffer = ArrayPool<byte>.Shared.Rent(checked((int)header.AssetEntrySize));
        try
        {
            for (var index = 0u; index < header.AssetCount; index++)
            {
                stream.ReadExactly(buffer.AsSpan(0, checked((int)header.AssetEntrySize)));
                var entry = buffer.AsSpan(0, checked((int)header.AssetEntrySize));
                var key = AssetKey.FromBytes(entry[..16]);
                var assetType = entry[16];
                var descriptorOffset = BinaryPrimitives.ReadUInt32LittleEndian(entry.Slice(17, 4));
                var virtualOffset = BinaryPrimitives.ReadUInt32LittleEndian(entry.Slice(21, 4));
                var sourcePath = ReadString(stringTable, BinaryPrimitives.ReadUInt32LittleEndian(entry.Slice(25, 4)));
                var descriptorSize = BinaryPrimitives.ReadUInt64LittleEndian(entry.Slice(29, 8));
                var sha = entry.Slice(37, Sha256Size).ToArray();
                var references = new AssetReferenceTable(
                    BinaryPrimitives.ReadUInt64LittleEndian(entry.Slice(69, 8)),
                    BinaryPrimitives.ReadUInt32LittleEndian(entry.Slice(77, 4)),
                    BinaryPrimitives.ReadUInt32LittleEndian(entry.Slice(81, 4)));
                assets.Add(new AssetEntry(
                    key,
                    ReadString(stringTable, descriptorOffset),
                    virtualOffset == 0 ? null : ReadString(stringTable, virtualOffset),
                    assetType,
                    descriptorSize,
                    sha)
                {
                    References = references,
                    SourcePath = sourcePath.Length == 0 ? null : sourcePath,
                });
            }
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(buffer);
        }

        return assets;
    }

    private static List<FileRecord> ReadFiles(Stream stream, HeaderFields header, byte[] stringTable)
    {
        var files = new List<FileRecord>(checked((int)header.FileRecordCount));
        stream.Position = checked((long)header.FileRecordsOffset);
        var buffer = ArrayPool<byte>.Shared.Rent(checked((int)header.FileRecordSize));
        try
        {
            for (var index = 0u; index < header.FileRecordCount; index++)
            {
                stream.ReadExactly(buffer.AsSpan(0, checked((int)header.FileRecordSize)));
                var record = buffer.AsSpan(0, checked((int)header.FileRecordSize));
                var rawKind = BinaryPrimitives.ReadUInt16LittleEndian(record[..2]);
                var kind = (FileKind)rawKind;
                if (!Enum.IsDefined(kind))
                {
                    throw new InvalidDataException($"Unknown loose-index file kind {rawKind}.");
                }

                var size = BinaryPrimitives.ReadUInt64LittleEndian(record.Slice(2, 8));
                var pathOffset = BinaryPrimitives.ReadUInt32LittleEndian(record.Slice(10, 4));
                var virtualPath = ReadString(stringTable, BinaryPrimitives.ReadUInt32LittleEndian(record.Slice(14, 4)));
                var sourcePath = ReadString(stringTable, BinaryPrimitives.ReadUInt32LittleEndian(record.Slice(18, 4)));
                files.Add(new(
                    kind,
                    ReadString(stringTable, pathOffset),
                    size,
                    record.Slice(22, Sha256Size).ToArray(),
                    virtualPath.Length == 0 ? null : virtualPath,
                    sourcePath.Length == 0 ? null : sourcePath));
            }
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(buffer);
        }

        return files;
    }

    private static void ValidateDocument(Document document)
    {
        var paths = new HashSet<string>(StringComparer.Ordinal);
        var keys = new HashSet<AssetKey>();
        var virtualPaths = new HashSet<string>(StringComparer.Ordinal);
        var roles = new HashSet<FileKind>();
        foreach (var asset in document.Assets)
        {
            ValidateMember(asset.DescriptorRelativePath, asset.DescriptorSha256, paths);
            if (!keys.Add(asset.AssetKey))
            {
                throw new InvalidDataException($"The cooked index contains a duplicate asset key: {asset.AssetKey}.");
            }

            if (string.IsNullOrEmpty(asset.VirtualPath) || !virtualPaths.Add(asset.VirtualPath))
            {
                throw new InvalidDataException($"The cooked index contains a missing or duplicate virtual path: {asset.VirtualPath}.");
            }

            if (string.IsNullOrEmpty(asset.SourcePath))
            {
                throw new InvalidDataException($"The cooked index records no producing source for {asset.VirtualPath}.");
            }
        }

        foreach (var file in document.Files)
        {
            ValidateMember(file.RelativePath, file.Sha256, paths);
            var pathKeyed = file.Kind.IsPathKeyed();
            if (file.Kind == FileKind.Unknown || (!pathKeyed && !roles.Add(file.Kind)))
            {
                throw new InvalidDataException("The cooked index contains an unknown or duplicate file role.");
            }

            if ((file.Kind == FileKind.TextureDescriptor) != (file.VirtualPath is not null)
                || (file.VirtualPath is not null) != (file.SourcePath is not null)
                || (file.VirtualPath is not null && !virtualPaths.Add(file.VirtualPath)))
            {
                throw new InvalidDataException($"The cooked index contains a missing, unexpected or duplicate file virtual path: {file.RelativePath}.");
            }
        }

        foreach (var (table, data) in new[]
        {
            (FileKind.BuffersTable, FileKind.BuffersData),
            (FileKind.TexturesTable, FileKind.TexturesData),
            (FileKind.ScriptsTable, FileKind.ScriptsData),
            (FileKind.PhysicsTable, FileKind.PhysicsData),
        })
        {
            if (roles.Contains(table) != roles.Contains(data))
            {
                throw new InvalidDataException($"The cooked index must contain both {table} and {data}.");
            }
        }
    }

    private static void ValidateMember(string path, ReadOnlyMemory<byte> digest, HashSet<string> paths)
    {
        if (string.IsNullOrEmpty(path) || path.Contains('\\') || path.Contains(':')
            || path.Split('/').Any(static segment => segment is "" or "." or "..")
            || path is "container.index.bin" or ".generation.lock" || !paths.Add(path))
        {
            throw new InvalidDataException($"The cooked index contains an invalid or duplicate member path: {path}.");
        }

        if (digest.Length != Sha256Size || digest.Span.IndexOfAnyExcept((byte)0) < 0)
        {
            throw new InvalidDataException($"The cooked member is missing its SHA-256 digest: {path}.");
        }
    }

    private static Guid ReadCanonicalGuid(ReadOnlySpan<byte> bytes) => new(bytes, bigEndian: true);

    private static byte[] ReadBlock(Stream stream, ulong offset, int size)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(size);
        var rented = ArrayPool<byte>.Shared.Rent(size);
        try
        {
            stream.Position = checked((long)offset);
            stream.ReadExactly(rented.AsSpan(0, size));
            return rented.AsSpan(0, size).ToArray();
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(rented);
        }
    }

    private static string ReadString(byte[] stringTable, uint offset)
    {
        if (offset >= (uint)stringTable.Length)
        {
            throw new InvalidDataException("String offset outside string table.");
        }

        var start = checked((int)offset);
        var end = start;
        while (end < stringTable.Length && stringTable[end] != 0)
        {
            end++;
        }

        if (end >= stringTable.Length)
        {
            throw new InvalidDataException("Unterminated string in string table.");
        }

        return Encoding.UTF8.GetString(stringTable, start, end - start);
    }

    private static void ValidateRange(ulong offset, ulong size, ulong fileSize, string label)
    {
        checked
        {
            if (offset + size > fileSize)
            {
                throw new InvalidDataException($"{label} range exceeds file size.");
            }
        }
    }

    private readonly record struct HeaderFields(
        ushort ContentVersion,
        IndexFeatures Flags,
        Guid SourceGuid,
        ulong StringTableOffset,
        ulong StringTableSize,
        ulong AssetEntriesOffset,
        uint AssetCount,
        uint AssetEntrySize,
        ulong FileRecordsOffset,
        uint FileRecordCount,
        uint FileRecordSize);
}
