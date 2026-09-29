// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers;
using System.Buffers.Binary;
using System.Text;

namespace Oxygen.Managed.Assets.Persistence.LooseCooked.V2;

/// <summary>
/// Runtime-compatible metadata reader for <c>container.index.bin</c> (LooseCookedIndex v2).
/// </summary>
/// <remarks>
/// The authoritative format specification is the C++ header
/// <c>Oxygen/Data/LooseCookedIndexFormat.h</c>.
/// </remarks>
public static class LooseCookedIndex
{
    // Must match oxygen::data::loose_cooked::IndexHeader (see Oxygen/Data/LooseCookedIndexFormat.h).
    public const int HeaderSize = 256;
    public const int AssetEntrySize = 65;
    public const int FileRecordSize = 46;
    public const int Sha256Size = 32;

    private static readonly byte[] HeaderMagic = Encoding.ASCII.GetBytes("OXLCIDX\0");

    /// <summary>
    /// Reads a v2 loose cooked index document from a stream.
    /// </summary>
    /// <param name="stream">The seekable stream containing the index.</param>
    /// <returns>The parsed document.</returns>
    public static Document Read(Stream stream)
    {
        ArgumentNullException.ThrowIfNull(stream);

        EnsureSeekable(stream, nameof(Read));
        var length = EnsureHasHeader(stream);

        var headerFields = ReadHeaderFields(stream);
        ValidateHeaderFields(headerFields, length);

        var stringTable = ReadStringTable();
        var assets = ReadAssetsSection();
        var files = ReadFileRecordsSection();

        var document = new Document(headerFields.ContentVersion, headerFields.Flags, headerFields.SourceGuid, assets, files);
        ValidateDocument(document);
        return document;

        byte[] ReadStringTable() => ReadBlock(stream, headerFields.StringTableOffset, checked((int)headerFields.StringTableSize));

        List<AssetEntry> ReadAssetsSection() => ReadAssets(
            stream,
            headerFields.AssetEntriesOffset,
            headerFields.AssetCount,
            headerFields.AssetEntrySize,
            stringTable);

        List<FileRecord> ReadFileRecordsSection() => headerFields.FileRecordCount == 0
            ? new List<FileRecord>()
            : ReadFileRecords(
                stream,
                headerFields.FileRecordsOffset,
                headerFields.FileRecordCount,
                headerFields.FileRecordSize,
                stringTable);
    }

    private static void EnsureSeekable(Stream stream, string caller)
    {
        if (!stream.CanSeek)
        {
            throw new NotSupportedException($"{nameof(LooseCookedIndex)}.{caller} requires a seekable stream.");
        }
    }

    private static ulong EnsureHasHeader(Stream stream)
    {
        var length = (ulong)stream.Length;
        if (length < HeaderSize)
        {
            throw new InvalidDataException("Index is too small to contain a header.");
        }

        return length;
    }

    private static void ValidateKnownFlags(uint flagsRaw)
    {
        // When flags != 0, enforce known bits (runtime guidance).
        var knownMask = (uint)(IndexFeatures.HasVirtualPaths | IndexFeatures.HasFileRecords);
        if (flagsRaw != 0 && (flagsRaw & ~knownMask) != 0)
        {
            throw new InvalidDataException("Index flags contain unknown bits.");
        }
    }

    private static HeaderFields ReadHeaderFields(Stream stream)
    {
        Span<byte> header = stackalloc byte[HeaderSize];
        stream.Position = 0;
        stream.ReadExactly(header);

        if (!header[..8].SequenceEqual(HeaderMagic))
        {
            throw new InvalidDataException("Invalid LooseCookedIndex header magic.");
        }

        var version = BinaryPrimitives.ReadUInt16LittleEndian(header.Slice(8, 2));
        if (version != 2)
        {
            throw new NotSupportedException($"Unsupported LooseCookedIndex version {version}.");
        }

        var contentVersion = BinaryPrimitives.ReadUInt16LittleEndian(header.Slice(10, 2));
        var flagsRaw = BinaryPrimitives.ReadUInt32LittleEndian(header.Slice(12, 4));
        var flags = (IndexFeatures)flagsRaw;

        var sourceGuid = ReadCanonicalGuid(header.Slice(16, 16));

        var stringTableOffset = BinaryPrimitives.ReadUInt64LittleEndian(header.Slice(32, 8));
        var stringTableSize = BinaryPrimitives.ReadUInt64LittleEndian(header.Slice(40, 8));

        var assetEntriesOffset = BinaryPrimitives.ReadUInt64LittleEndian(header.Slice(48, 8));
        var assetCount = BinaryPrimitives.ReadUInt32LittleEndian(header.Slice(56, 4));
        var assetEntrySize = BinaryPrimitives.ReadUInt32LittleEndian(header.Slice(60, 4));

        var fileRecordsOffset = BinaryPrimitives.ReadUInt64LittleEndian(header.Slice(64, 8));
        var fileRecordCount = BinaryPrimitives.ReadUInt32LittleEndian(header.Slice(72, 4));
        var fileRecordSize = BinaryPrimitives.ReadUInt32LittleEndian(header.Slice(76, 4));

        ValidateKnownFlags(flagsRaw);

        return new HeaderFields(
            contentVersion,
            flags,
            stringTableOffset,
            stringTableSize,
            assetEntriesOffset,
            assetCount,
            assetEntrySize,
            fileRecordsOffset,
            fileRecordCount,
            fileRecordSize,
            sourceGuid);
    }

    private static void ValidateHeaderFields(HeaderFields headerFields, ulong length)
    {
        // Guid.Variant exposes the high nibble; RFC UUIDs use the 10xx bit pattern.
        if (headerFields.SourceGuid.Version != 7 || headerFields.SourceGuid.Variant is < 8 or > 11)
        {
            throw new InvalidDataException("The cooked index requires a UUIDv7 source identity.");
        }

        const IndexFeatures known = IndexFeatures.HasVirtualPaths | IndexFeatures.HasFileRecords;
        if ((headerFields.Flags & ~known) != 0 || (headerFields.Flags & IndexFeatures.HasVirtualPaths) == 0
            || (headerFields.FileRecordCount > 0 && (headerFields.Flags & IndexFeatures.HasFileRecords) == 0))
        {
            throw new InvalidDataException("The cooked index has invalid section flags.");
        }

        ValidateRange(headerFields.StringTableOffset, headerFields.StringTableSize, length, "string table");

        if (headerFields.AssetEntrySize != AssetEntrySize)
        {
            throw new InvalidDataException(
                $"Asset entry size {headerFields.AssetEntrySize} does not match v2 size {AssetEntrySize}.");
        }

        checked
        {
            var assetsBytes = (ulong)headerFields.AssetCount * headerFields.AssetEntrySize;
            ValidateRange(headerFields.AssetEntriesOffset, assetsBytes, length, "asset entries");

            var fileRecordsMinimumOffset = headerFields.AssetEntriesOffset + assetsBytes;
            if (headerFields.FileRecordsOffset < fileRecordsMinimumOffset)
            {
                throw new InvalidDataException("File records must start after the end of the asset entries.");
            }
        }

        if (headerFields.FileRecordCount > 0)
        {
            if (headerFields.FileRecordSize != FileRecordSize)
            {
                throw new InvalidDataException(
                    $"File record size {headerFields.FileRecordSize} does not match v2 size {FileRecordSize}.");
            }

            checked
            {
                var filesBytes = (ulong)headerFields.FileRecordCount * headerFields.FileRecordSize;
                ValidateRange(headerFields.FileRecordsOffset, filesBytes, length, "file records");
            }
        }
    }

    private static List<AssetEntry> ReadAssets(
        Stream stream,
        ulong assetEntriesOffset,
        uint assetCount,
        uint assetEntrySize,
        byte[] stringTable)
    {
        var assets = new List<AssetEntry>((int)assetCount);
        stream.Position = (long)assetEntriesOffset;

        var assetBuffer = ArrayPool<byte>.Shared.Rent(checked((int)assetEntrySize));
        try
        {
            for (var i = 0u; i < assetCount; i++)
            {
                stream.ReadExactly(assetBuffer.AsSpan(0, (int)assetEntrySize));
                var entrySpan = (ReadOnlySpan<byte>)assetBuffer.AsSpan(0, (int)assetEntrySize);

                var key = AssetKey.FromBytes(entrySpan[..16]);
                var assetType = entrySpan[16];
                var descRelOffset = BinaryPrimitives.ReadUInt32LittleEndian(entrySpan.Slice(17, 4));
                var virtualOffset = BinaryPrimitives.ReadUInt32LittleEndian(entrySpan.Slice(21, 4));
                var descriptorSize = BinaryPrimitives.ReadUInt64LittleEndian(entrySpan.Slice(25, 8));
                var sha = entrySpan.Slice(33, Sha256Size).ToArray();

                var descRel = ReadNullTerminatedUtf8(stringTable, descRelOffset);
                var virtualPath = virtualOffset == 0 ? null : ReadNullTerminatedUtf8(stringTable, virtualOffset);

                assets.Add(new AssetEntry(key, descRel, virtualPath, assetType, descriptorSize, sha));
            }
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(assetBuffer);
        }

        return assets;
    }

    private static List<FileRecord> ReadFileRecords(
        Stream stream,
        ulong fileRecordsOffset,
        uint fileRecordCount,
        uint fileRecordSize,
        byte[] stringTable)
    {
        var files = new List<FileRecord>((int)fileRecordCount);
        stream.Position = (long)fileRecordsOffset;

        var fileBuffer = ArrayPool<byte>.Shared.Rent(checked((int)fileRecordSize));
        try
        {
            for (var i = 0u; i < fileRecordCount; i++)
            {
                stream.ReadExactly(fileBuffer.AsSpan(0, (int)fileRecordSize));
                var recordSpan = (ReadOnlySpan<byte>)fileBuffer.AsSpan(0, (int)fileRecordSize);

                var kind = (FileKind)BinaryPrimitives.ReadUInt16LittleEndian(recordSpan[..2]);
                var size = BinaryPrimitives.ReadUInt64LittleEndian(recordSpan.Slice(2, 8));
                var relOffset = BinaryPrimitives.ReadUInt32LittleEndian(recordSpan.Slice(10, 4));
                var relPath = ReadNullTerminatedUtf8(stringTable, relOffset);
                files.Add(new FileRecord(kind, relPath, size, recordSpan.Slice(14, Sha256Size).ToArray()));
            }
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(fileBuffer);
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
        }

        foreach (var file in document.Files)
        {
            ValidateMember(file.RelativePath, file.Sha256, paths);
            if (!Enum.IsDefined(file.Kind) || file.Kind == FileKind.Unknown || (file.Kind != FileKind.Auxiliary && !roles.Add(file.Kind)))
            {
                throw new InvalidDataException("The cooked index contains an unknown or duplicate file role.");
            }
        }

        foreach (var (table, data) in new[] { (FileKind.BuffersTable, FileKind.BuffersData), (FileKind.TexturesTable, FileKind.TexturesData),
            (FileKind.ScriptsTable, FileKind.ScriptsData), (FileKind.PhysicsTable, FileKind.PhysicsData), (FileKind.ScriptBindingsTable, FileKind.ScriptBindingsData) })
        {
            if (roles.Contains(table) != roles.Contains(data))
            {
                throw new InvalidDataException($"The cooked index must contain both {table} and {data}.");
            }
        }
    }

    private static void ValidateMember(string path, ReadOnlyMemory<byte> digest, HashSet<string> paths)
    {
        if (string.IsNullOrEmpty(path) || path.Contains('\\') || path.Contains(':') || path.Split('/').Any(static segment => segment is "" or "." or "..")
            || path is "container.index.bin" or ".generation.lock" || !paths.Add(path))
        {
            throw new InvalidDataException($"The cooked index contains an invalid or duplicate member path: {path}.");
        }

        if (digest.Length != Sha256Size || digest.Span.IndexOfAnyExcept((byte)0) < 0)
        {
            throw new InvalidDataException($"The cooked member is missing its SHA-256 digest: {path}.");
        }
    }

    private static Guid ReadCanonicalGuid(ReadOnlySpan<byte> bytes)
        => new(bytes, bigEndian: true);

    private static byte[] ReadBlock(Stream stream, ulong offset, int size)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(size);

        var rented = ArrayPool<byte>.Shared.Rent(size);
        try
        {
            stream.Position = (long)offset;
            stream.ReadExactly(rented.AsSpan(0, size));
            return rented.AsSpan(0, size).ToArray();
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(rented);
        }
    }

    private static string ReadNullTerminatedUtf8(byte[] stringTable, uint offset)
    {
        if (offset >= (uint)stringTable.Length)
        {
            throw new InvalidDataException("String offset outside string table.");
        }

        var start = (int)offset;
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
            var end = offset + size;
            if (end > fileSize)
            {
                throw new InvalidDataException($"{label} range exceeds file size.");
            }
        }
    }

    private readonly record struct HeaderFields(
        ushort ContentVersion,
        IndexFeatures Flags,
        ulong StringTableOffset,
        ulong StringTableSize,
        ulong AssetEntriesOffset,
        uint AssetCount,
        uint AssetEntrySize,
        ulong FileRecordsOffset,
        uint FileRecordCount,
        uint FileRecordSize,
        Guid SourceGuid);

}
