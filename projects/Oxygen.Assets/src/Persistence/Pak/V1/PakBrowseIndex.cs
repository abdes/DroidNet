// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers.Binary;
using System.Runtime.InteropServices;
using System.Text;
using Oxygen.Assets.Persistence.LooseCooked.V1;

namespace Oxygen.Assets.Persistence.Pak.V1;

/// <summary>
/// Embedded browse index for PAK files (v1).
/// </summary>
/// <remarks>
/// This structure provides a mapping of <see cref="AssetKey"/> to a canonical virtual path
/// (e.g. <c>/Content/Foo.bar</c>) for editor browsing and diagnostics.
/// </remarks>
internal sealed class PakBrowseIndex
{
    private const uint Version = 1;

    private static readonly byte[] Magic = Encoding.ASCII.GetBytes("OXPAKBIX");

    private PakBrowseIndex(IReadOnlyList<Entry> entries)
    {
        this.Entries = entries;
    }

    /// <summary>
    /// Gets the browse index entries.
    /// </summary>
    public IReadOnlyList<Entry> Entries { get; }

    /// <summary>
    /// Reads a browse index from the given stream.
    /// </summary>
    /// <param name="stream">The input stream.</param>
    /// <returns>The parsed browse index.</returns>
    public static PakBrowseIndex Read(Stream stream)
    {
        ArgumentNullException.ThrowIfNull(stream);

        Span<byte> header = stackalloc byte[24];
        ReadExactly(stream, header);

        if (!header[..8].SequenceEqual(Magic))
        {
            throw new InvalidDataException("Browse index magic is invalid.");
        }

        var version = BinaryPrimitives.ReadUInt32LittleEndian(header.Slice(8, 4));
        if (version != Version)
        {
            throw new InvalidDataException($"Unsupported browse index version {version}.");
        }

        var count = BinaryPrimitives.ReadUInt32LittleEndian(header.Slice(12, 4));
        var stringsSize = BinaryPrimitives.ReadUInt32LittleEndian(header.Slice(16, 4));

        // header.Slice(20,4) reserved
        var entriesSize = checked((int)count * 24);
        var entriesBytes = new byte[entriesSize];
        ReadExactly(stream, entriesBytes);

        var stringsBytes = new byte[stringsSize];
        ReadExactly(stream, stringsBytes);

        var entries = new Entry[count];
        for (var i = 0; i < count; i++)
        {
            var entrySpan = entriesBytes.AsSpan(i * 24, 24);
            var key = AssetKey.FromBytes(entrySpan[..16]);
            var pathOffset = BinaryPrimitives.ReadUInt32LittleEndian(entrySpan.Slice(16, 4));
            var pathLength = BinaryPrimitives.ReadUInt32LittleEndian(entrySpan.Slice(20, 4));

            if (pathOffset > stringsSize || pathLength > stringsSize || pathOffset + pathLength > stringsSize)
            {
                throw new InvalidDataException("Browse index string reference is out of bounds.");
            }

            var virtualPath = Encoding.UTF8.GetString(stringsBytes, (int)pathOffset, (int)pathLength);
            entries[i] = new Entry(key, virtualPath);
        }

        return new PakBrowseIndex(entries);
    }

    /// <summary>
    /// Writes a browse index to the given stream.
    /// </summary>
    /// <param name="stream">The output stream.</param>
    /// <param name="entries">The entries to write.</param>
    public static void Write(Stream stream, IReadOnlyList<Entry> entries)
    {
        ArgumentNullException.ThrowIfNull(stream);
        ArgumentNullException.ThrowIfNull(entries);

        // Build string table.
        var strings = new List<byte>(capacity: entries.Count * 32);
        var pathSpans = new (uint offset, uint length)[entries.Count];

        for (var i = 0; i < entries.Count; i++)
        {
            var path = entries[i].VirtualPath;
            if (string.IsNullOrEmpty(path) || path[0] != '/')
            {
                throw new InvalidDataException("VirtualPath must start with '/'.");
            }

            var bytes = Encoding.UTF8.GetBytes(path);
            pathSpans[i] = ((uint)strings.Count, (uint)bytes.Length);
            strings.AddRange(bytes);
        }

        Span<byte> header = stackalloc byte[24];
        Magic.CopyTo(header);
        BinaryPrimitives.WriteUInt32LittleEndian(header.Slice(8, 4), Version);
        BinaryPrimitives.WriteUInt32LittleEndian(header.Slice(12, 4), (uint)entries.Count);
        BinaryPrimitives.WriteUInt32LittleEndian(header.Slice(16, 4), (uint)strings.Count);
        BinaryPrimitives.WriteUInt32LittleEndian(header.Slice(20, 4), 0);
        stream.Write(header);

        Span<byte> entryBytes = stackalloc byte[24];
        for (var i = 0; i < entries.Count; i++)
        {
            entryBytes.Clear();
            entries[i].AssetKey.WriteBytes(entryBytes[..16]);
            BinaryPrimitives.WriteUInt32LittleEndian(entryBytes.Slice(16, 4), pathSpans[i].offset);
            BinaryPrimitives.WriteUInt32LittleEndian(entryBytes.Slice(20, 4), pathSpans[i].length);
            stream.Write(entryBytes);
        }

        stream.Write(strings.ToArray());
    }

    private static void ReadExactly(Stream stream, Span<byte> buffer)
    {
        var totalRead = 0;
        while (totalRead < buffer.Length)
        {
            var read = stream.Read(buffer[totalRead..]);
            if (read == 0)
            {
                throw new EndOfStreamException("Unexpected end of stream.");
            }

            totalRead += read;
        }
    }

    private static void ReadExactly(Stream stream, byte[] buffer)
        => ReadExactly(stream, buffer.AsSpan());

    /// <summary>
    /// A browse index entry mapping an asset key to a virtual path.
    /// </summary>
    internal readonly record struct Entry(AssetKey AssetKey, string VirtualPath);
}
