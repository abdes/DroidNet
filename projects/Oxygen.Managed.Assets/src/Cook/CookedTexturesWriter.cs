// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers.Binary;
using Oxygen.Managed.Assets.Persistence.LooseCooked.V1;

namespace Oxygen.Managed.Assets.Cook;

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

            var entry = new byte[TableEntrySize];
            BinaryPrimitives.WriteUInt64LittleEndian(entry.AsSpan(0, 8), offset);
            BinaryPrimitives.WriteUInt32LittleEndian(entry.AsSpan(8, 4), size);
            entry[12] = item.TextureType;
            entry[13] = item.CompressionType;
            BinaryPrimitives.WriteUInt32LittleEndian(entry.AsSpan(16, 4), item.Width);
            BinaryPrimitives.WriteUInt32LittleEndian(entry.AsSpan(20, 4), item.Height);
            BinaryPrimitives.WriteUInt16LittleEndian(entry.AsSpan(24, 2), item.Depth);
            BinaryPrimitives.WriteUInt16LittleEndian(entry.AsSpan(26, 2), item.ArrayLayers);
            BinaryPrimitives.WriteUInt16LittleEndian(entry.AsSpan(28, 2), item.MipLevels);
            entry[30] = item.Format;
            BinaryPrimitives.WriteUInt16LittleEndian(entry.AsSpan(31, 2), item.Alignment);

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
