// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers.Binary;
using Oxygen.Assets.Persistence.LooseCooked.V1;

namespace Oxygen.Assets.Cook;

public readonly record struct TextureCookInput(
    AssetKey AssetKey,
    ReadOnlyMemory<byte> DataBytes,
    uint Width,
    uint Height,
    ushort Depth,
    ushort ArrayLayers,
    ushort MipLevels,
    byte TextureType,
    byte CompressionType,
    byte Format,
    ushort Alignment);

public readonly record struct CookedTexturesResult(
    ReadOnlyMemory<byte> TableBytes,
    ReadOnlyMemory<byte> DataBytes,
    Dictionary<AssetKey, uint> Indices);

public static class CookedTexturesWriter
{
    // Matches oxygen::data::pak::v1::TextureResourceDesc (packed, 40 bytes).
    // For loose cooked content, offsets are interpreted against the textures.data stream.
    private const int TableEntrySize = 40;
    private const int TextureAlignmentBytes = 256;

    public static CookedTexturesResult Write(IReadOnlyList<TextureCookInput> textures)
    {
        var mapping = new Dictionary<AssetKey, uint>();
        using var dataStream = new MemoryStream();
        var tableEntries = new List<byte[]> { new byte[TableEntrySize] }; // Index 0 reserved

        foreach (var item in textures)
        {
            var index = (uint)tableEntries.Count;
            mapping[item.AssetKey] = index;

            AlignStream(dataStream, item.Alignment);

            var offset = (ulong)dataStream.Position;
            dataStream.Write(item.DataBytes.Span);
            var size = (uint)item.DataBytes.Length;

            // Create table entry
            var entry = new byte[TableEntrySize];
            // 0-8: data_offset
            BinaryPrimitives.WriteUInt64LittleEndian(entry.AsSpan(0, 8), offset);
            // 8-12: size_bytes
            BinaryPrimitives.WriteUInt32LittleEndian(entry.AsSpan(8, 4), size);
            // 12-13: texture_type
            entry[12] = item.TextureType;
            // 13-14: compression_type
            entry[13] = item.CompressionType;
            // 14-16: padding (packed struct alignment)
            // 16-20: width
            BinaryPrimitives.WriteUInt32LittleEndian(entry.AsSpan(16, 4), item.Width);
            // 20-24: height
            BinaryPrimitives.WriteUInt32LittleEndian(entry.AsSpan(20, 4), item.Height);
            // 24-26: depth
            BinaryPrimitives.WriteUInt16LittleEndian(entry.AsSpan(24, 2), item.Depth);
            // 26-28: array_layers
            BinaryPrimitives.WriteUInt16LittleEndian(entry.AsSpan(26, 2), item.ArrayLayers);
            // 28-30: mip_levels
            BinaryPrimitives.WriteUInt16LittleEndian(entry.AsSpan(28, 2), item.MipLevels);
            // 30-31: format (oxygen::Format)
            entry[30] = item.Format;
            // 31-33: alignment
            BinaryPrimitives.WriteUInt16LittleEndian(entry.AsSpan(31, 2), item.Alignment);
            // 33-42: reserved[9] (zero)

            tableEntries.Add(entry);
        }

        // Flatten table
        var tableBytes = new byte[tableEntries.Count * TableEntrySize];
        for (var i = 0; i < tableEntries.Count; i++)
        {
            tableEntries[i].CopyTo(tableBytes, i * TableEntrySize);
        }

        return new CookedTexturesResult(tableBytes, dataStream.ToArray(), mapping);
    }

    private static void AlignStream(Stream stream, int alignmentBytes)
    {
        if (alignmentBytes <= 1)
        {
            return;
        }

        var padding = (alignmentBytes - (stream.Position % alignmentBytes)) % alignmentBytes;
        if (padding == 0)
        {
            return;
        }

        stream.Write(new byte[padding]);
    }
}
