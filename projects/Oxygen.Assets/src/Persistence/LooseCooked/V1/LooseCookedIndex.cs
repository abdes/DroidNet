// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers;
using System.Buffers.Binary;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;

namespace Oxygen.Assets.Persistence.LooseCooked.V1;

/// <summary>
/// Runtime-compatible reader/writer for <c>container.index.bin</c> (LooseCookedIndex v1).
/// </summary>
/// <remarks>
/// The authoritative format specification is the C++ header
/// <c>Oxygen/Data/LooseCookedIndexFormat.h</c>.
/// </remarks>
public static class LooseCookedIndex
{
    public const int HeaderSize = 80;
    public const int AssetEntrySize = 76;
    public const int FileRecordSize = 64;
    public const int Sha256Size = 32;

    private static readonly byte[] HeaderMagic = Encoding.ASCII.GetBytes("OXLCIDX\0");

    /// <summary>
    /// Reads a v1 loose cooked index document from a stream.
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

        return new Document(headerFields.ContentVersion, headerFields.Flags, assets, files);

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

    /// <summary>
    /// Writes a v1 loose cooked index document to a stream.
    /// </summary>
    /// <param name="stream">The seekable target stream.</param>
    /// <param name="document">The document to write.</param>
    public static void Write(Stream stream, Document document)
    {
        ArgumentNullException.ThrowIfNull(stream);
        ArgumentNullException.ThrowIfNull(document);

        EnsureSeekable(stream, nameof(Write));

        var computedFlags = ComputeEffectiveFlags(document);
        var (stringTableBytes, offsets) = BuildStringTable(document);
        var layout = ComputeLayout(document, stringTableBytes.Length);

        WriteHeader(computedFlags);
        WriteStringTable();
        WriteAssets();
        WriteFiles();

        stream.Flush();

        void WriteHeader(IndexFeatures flags)
        {
            stream.SetLength(0);
            stream.Position = 0;
            WriteV1Header(
                stream,
                document.ContentVersion,
                flags,
                layout.StringTableOffset,
                (ulong)stringTableBytes.Length,
                layout.AssetEntriesOffset,
                (uint)document.Assets.Count,
                layout.FileRecordsOffset,
                (uint)document.Files.Count);
        }

        void WriteStringTable()
        {
            stream.Position = (long)layout.StringTableOffset;
            stream.Write(stringTableBytes);
            PadTo(stream, layout.AssetEntriesOffset);
        }

        void WriteAssets()
        {
            stream.Position = (long)layout.AssetEntriesOffset;
            WriteAssetsSection(stream, document, offsets);
            PadTo(stream, layout.FileRecordsOffset);
        }

        void WriteFiles()
        {
            if (document.Files.Count == 0)
            {
                return;
            }

            stream.Position = (long)layout.FileRecordsOffset;
            WriteFileRecordsSection(stream, document, offsets);
        }
    }

    /// <summary>
    /// Computes the SHA-256 hash of the provided bytes.
    /// </summary>
    /// <param name="bytes">The input bytes.</param>
    /// <returns>The 32-byte SHA-256 hash.</returns>
    public static byte[] ComputeSha256(ReadOnlySpan<byte> bytes)
    {
        var hash = SHA256.HashData(bytes);
        return hash;
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
        if (version != 1)
        {
            throw new NotSupportedException($"Unsupported LooseCookedIndex version {version}.");
        }

        var contentVersion = BinaryPrimitives.ReadUInt16LittleEndian(header.Slice(10, 2));
        var flagsRaw = BinaryPrimitives.ReadUInt32LittleEndian(header.Slice(12, 4));
        var flags = (IndexFeatures)flagsRaw;

        var stringTableOffset = BinaryPrimitives.ReadUInt64LittleEndian(header.Slice(16, 8));
        var stringTableSize = BinaryPrimitives.ReadUInt64LittleEndian(header.Slice(24, 8));

        var assetEntriesOffset = BinaryPrimitives.ReadUInt64LittleEndian(header.Slice(32, 8));
        var assetCount = BinaryPrimitives.ReadUInt32LittleEndian(header.Slice(40, 4));
        var assetEntrySize = BinaryPrimitives.ReadUInt32LittleEndian(header.Slice(44, 4));

        var fileRecordsOffset = BinaryPrimitives.ReadUInt64LittleEndian(header.Slice(48, 8));
        var fileRecordCount = BinaryPrimitives.ReadUInt32LittleEndian(header.Slice(56, 4));
        var fileRecordSize = BinaryPrimitives.ReadUInt32LittleEndian(header.Slice(60, 4));

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
            fileRecordSize);
    }

    private static void ValidateHeaderFields(HeaderFields headerFields, ulong length)
    {
        ValidateRange(headerFields.StringTableOffset, headerFields.StringTableSize, length, "string table");

        if (headerFields.AssetEntrySize < AssetEntrySize)
        {
            throw new InvalidDataException(
                $"Asset entry size {headerFields.AssetEntrySize} is smaller than v1 minimum {AssetEntrySize}.");
        }

        checked
        {
            var assetsBytes = (ulong)headerFields.AssetCount * headerFields.AssetEntrySize;
            ValidateRange(headerFields.AssetEntriesOffset, assetsBytes, length, "asset entries");
        }

        if (headerFields.FileRecordCount > 0)
        {
            if (headerFields.FileRecordSize < FileRecordSize)
            {
                throw new InvalidDataException(
                    $"File record size {headerFields.FileRecordSize} is smaller than v1 minimum {FileRecordSize}.");
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
                var descRelOffset = BinaryPrimitives.ReadUInt32LittleEndian(entrySpan.Slice(16, 4));
                var virtualOffset = BinaryPrimitives.ReadUInt32LittleEndian(entrySpan.Slice(20, 4));
                var assetType = entrySpan[24];
                var descriptorSize = BinaryPrimitives.ReadUInt64LittleEndian(entrySpan.Slice(28, 8));
                var sha = entrySpan.Slice(36, Sha256Size).ToArray();

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
                var relOffset = BinaryPrimitives.ReadUInt32LittleEndian(recordSpan.Slice(4, 4));
                var size = BinaryPrimitives.ReadUInt64LittleEndian(recordSpan.Slice(8, 8));
                var sha = recordSpan.Slice(16, Sha256Size).ToArray();
                var relPath = ReadNullTerminatedUtf8(stringTable, relOffset);
                files.Add(new FileRecord(kind, relPath, size, sha));
            }
        }
        finally
        {
            ArrayPool<byte>.Shared.Return(fileBuffer);
        }

        return files;
    }

    private static FileLayout ComputeLayout(Document document, int stringTableSize)
    {
        var stringTableOffset = (ulong)HeaderSize;
        var assetEntriesOffset = AlignUp(stringTableOffset + (ulong)stringTableSize, 8);
        var fileRecordsOffset = AlignUp(
            assetEntriesOffset + checked((ulong)document.Assets.Count * (ulong)AssetEntrySize),
            8);

        return new FileLayout(stringTableOffset, assetEntriesOffset, document.Files.Count > 0 ? fileRecordsOffset : 0);
    }

    private static IndexFeatures ComputeEffectiveFlags(Document document)
    {
        var flags = document.Flags;
        if (document.Files.Count > 0)
        {
            flags |= IndexFeatures.HasFileRecords;
        }

        if (document.Assets.Any(a => !string.IsNullOrEmpty(a.VirtualPath)))
        {
            flags |= IndexFeatures.HasVirtualPaths;
        }

        return flags;
    }

    private static (byte[] stringTableBytes, Dictionary<string, uint> offsets) BuildStringTable(Document document)
    {
        var strings = new SortedSet<string>(StringComparer.Ordinal);
        foreach (var asset in document.Assets)
        {
            strings.Add(asset.DescriptorRelativePath);
            if (!string.IsNullOrEmpty(asset.VirtualPath))
            {
                strings.Add(asset.VirtualPath);
            }
        }

        foreach (var file in document.Files)
        {
            strings.Add(file.RelativePath);
        }

        var offsets = new Dictionary<string, uint>(StringComparer.Ordinal);
        using var stringTableStream = new MemoryStream();

        // Reserve offset 0 as an empty string sentinel, allowing 0 to also represent "missing".
        stringTableStream.WriteByte(0);
        foreach (var s in strings)
        {
            offsets[s] = checked((uint)stringTableStream.Position);
            var bytes = Encoding.UTF8.GetBytes(s);
            stringTableStream.Write(bytes);
            stringTableStream.WriteByte(0);
        }

        return (stringTableStream.ToArray(), offsets);
    }

    private static void WriteV1Header(
        Stream stream,
        ushort contentVersion,
        IndexFeatures flags,
        ulong stringTableOffset,
        ulong stringTableSize,
        ulong assetEntriesOffset,
        uint assetCount,
        ulong fileRecordsOffset,
        uint fileRecordCount)
    {
        Span<byte> header = stackalloc byte[HeaderSize];
        header.Clear();
        HeaderMagic.CopyTo(header[..8]);
        BinaryPrimitives.WriteUInt16LittleEndian(header.Slice(8, 2), 1);
        BinaryPrimitives.WriteUInt16LittleEndian(header.Slice(10, 2), contentVersion);
        BinaryPrimitives.WriteUInt32LittleEndian(header.Slice(12, 4), (uint)flags);
        BinaryPrimitives.WriteUInt64LittleEndian(header.Slice(16, 8), stringTableOffset);
        BinaryPrimitives.WriteUInt64LittleEndian(header.Slice(24, 8), stringTableSize);
        BinaryPrimitives.WriteUInt64LittleEndian(header.Slice(32, 8), assetEntriesOffset);
        BinaryPrimitives.WriteUInt32LittleEndian(header.Slice(40, 4), assetCount);
        BinaryPrimitives.WriteUInt32LittleEndian(header.Slice(44, 4), (uint)AssetEntrySize);
        BinaryPrimitives.WriteUInt64LittleEndian(header.Slice(48, 8), fileRecordCount > 0 ? fileRecordsOffset : 0);
        BinaryPrimitives.WriteUInt32LittleEndian(header.Slice(56, 4), fileRecordCount);
        BinaryPrimitives.WriteUInt32LittleEndian(header.Slice(60, 4), (uint)FileRecordSize);
        stream.Write(header);
    }

    private static void WriteAssetsSection(Stream stream, Document document, Dictionary<string, uint> offsets)
    {
        Span<byte> entry = stackalloc byte[AssetEntrySize];
        foreach (var asset in document.Assets)
        {
            entry.Clear();
            asset.AssetKey.WriteBytes(entry[..16]);

            if (!offsets.TryGetValue(asset.DescriptorRelativePath, out var descOffset))
            {
                throw new InvalidOperationException("DescriptorRelativePath missing from string table.");
            }

            BinaryPrimitives.WriteUInt32LittleEndian(entry.Slice(16, 4), descOffset);
            if (!string.IsNullOrEmpty(asset.VirtualPath))
            {
                if (!offsets.TryGetValue(asset.VirtualPath, out var virtOffset))
                {
                    throw new InvalidOperationException("VirtualPath missing from string table.");
                }

                BinaryPrimitives.WriteUInt32LittleEndian(entry.Slice(20, 4), virtOffset);
            }

            entry[24] = asset.AssetType;
            BinaryPrimitives.WriteUInt64LittleEndian(entry.Slice(28, 8), asset.DescriptorSize);

            if (asset.DescriptorSha256.Length != Sha256Size)
            {
                throw new ArgumentException($"DescriptorSha256 must be {Sha256Size} bytes.", nameof(document));
            }

            asset.DescriptorSha256.CopyTo(entry.Slice(36, Sha256Size));
            stream.Write(entry);
        }
    }

    private static void WriteFileRecordsSection(Stream stream, Document document, Dictionary<string, uint> offsets)
    {
        Span<byte> record = stackalloc byte[FileRecordSize];
        foreach (var file in document.Files)
        {
            record.Clear();
            BinaryPrimitives.WriteUInt16LittleEndian(record[..2], (ushort)file.Kind);

            if (!offsets.TryGetValue(file.RelativePath, out var relOffset))
            {
                throw new InvalidOperationException("RelativePath missing from string table.");
            }

            BinaryPrimitives.WriteUInt32LittleEndian(record.Slice(4, 4), relOffset);
            BinaryPrimitives.WriteUInt64LittleEndian(record.Slice(8, 8), file.Size);

            if (file.Sha256.Length != Sha256Size)
            {
                throw new ArgumentException($"Sha256 must be {Sha256Size} bytes.", nameof(document));
            }

            file.Sha256.CopyTo(record.Slice(16, Sha256Size));
            stream.Write(record);
        }
    }

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

    private static ulong AlignUp(ulong value, ulong alignment)
    {
        if (alignment == 0)
        {
            return value;
        }

        var mask = alignment - 1;
        return (value + mask) & ~mask;
    }

    private static void PadTo(Stream stream, ulong absolutePosition)
    {
        if ((ulong)stream.Position > absolutePosition)
        {
            throw new InvalidOperationException("Attempted to pad backwards.");
        }

        while ((ulong)stream.Position < absolutePosition)
        {
            stream.WriteByte(0);
        }
    }

    [StructLayout(LayoutKind.Auto)]
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
        uint FileRecordSize);

    [StructLayout(LayoutKind.Auto)]
    private readonly record struct FileLayout(
        ulong StringTableOffset,
        ulong AssetEntriesOffset,
        ulong FileRecordsOffset);
}
