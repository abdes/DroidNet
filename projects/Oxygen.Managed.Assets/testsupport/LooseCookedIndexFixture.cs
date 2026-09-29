// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers.Binary;
using System.Text;
using Oxygen.Managed.Assets.Persistence.LooseCooked.V2;

namespace Oxygen.Testing;

/// <summary>Small binary fixture emitter for reader and orchestration tests; never shipped.</summary>
internal static class LooseCookedIndexFixture
{
    public static void Write(Stream destination, Document document)
    {
        using var strings = new MemoryStream();
        strings.WriteByte(0);
        var offsets = new Dictionary<string, uint>(StringComparer.Ordinal) { [string.Empty] = 0 };
        uint Add(string? value)
        {
            value ??= string.Empty;
            if (offsets.TryGetValue(value, out var existing)) { return existing; }
            var offset = checked((uint)strings.Length);
            strings.Write(Encoding.UTF8.GetBytes(value));
            strings.WriteByte(0);
            offsets.Add(value, offset);
            return offset;
        }
        foreach (var asset in document.Assets) { _ = Add(asset.DescriptorRelativePath); _ = Add(asset.VirtualPath); }
        foreach (var file in document.Files) { _ = Add(file.RelativePath); }
        var header = new byte[LooseCookedIndex.HeaderSize];
        "OXLCIDX\0"u8.CopyTo(header);
        BinaryPrimitives.WriteUInt16LittleEndian(header.AsSpan(8), 2);
        BinaryPrimitives.WriteUInt16LittleEndian(header.AsSpan(10), document.ContentVersion);
        var flags = document.Flags | IndexFeatures.HasVirtualPaths | IndexFeatures.HasFileRecords;
        BinaryPrimitives.WriteUInt32LittleEndian(header.AsSpan(12), (uint)flags);
        var source = document.SourceGuid.Version == 7 ? document.SourceGuid : Guid.CreateVersion7();
        source.ToByteArray(bigEndian: true).CopyTo(header, 16);
        BinaryPrimitives.WriteUInt64LittleEndian(header.AsSpan(32), checked((ulong)header.Length));
        BinaryPrimitives.WriteUInt64LittleEndian(header.AsSpan(40), checked((ulong)strings.Length));
        BinaryPrimitives.WriteUInt64LittleEndian(header.AsSpan(48), checked((ulong)(header.Length + strings.Length)));
        BinaryPrimitives.WriteUInt32LittleEndian(header.AsSpan(56), checked((uint)document.Assets.Count));
        BinaryPrimitives.WriteUInt32LittleEndian(header.AsSpan(60), LooseCookedIndex.AssetEntrySize);
        BinaryPrimitives.WriteUInt64LittleEndian(header.AsSpan(64), checked((ulong)(header.Length + strings.Length + (document.Assets.Count * LooseCookedIndex.AssetEntrySize))));
        BinaryPrimitives.WriteUInt32LittleEndian(header.AsSpan(72), checked((uint)document.Files.Count));
        BinaryPrimitives.WriteUInt32LittleEndian(header.AsSpan(76), LooseCookedIndex.FileRecordSize);
        destination.SetLength(0);
        destination.Write(header);
        destination.Write(strings.ToArray());
        foreach (var asset in document.Assets)
        {
            var entry = new byte[LooseCookedIndex.AssetEntrySize];
            asset.AssetKey.WriteBytes(entry.AsSpan(0, 16));
            entry[16] = asset.AssetType;
            BinaryPrimitives.WriteUInt32LittleEndian(entry.AsSpan(17), offsets[asset.DescriptorRelativePath]);
            BinaryPrimitives.WriteUInt32LittleEndian(entry.AsSpan(21), offsets[asset.VirtualPath ?? string.Empty]);
            BinaryPrimitives.WriteUInt64LittleEndian(entry.AsSpan(25), asset.DescriptorSize);
            asset.DescriptorSha256.Span.CopyTo(entry.AsSpan(33));
            destination.Write(entry);
        }
        foreach (var file in document.Files)
        {
            var entry = new byte[LooseCookedIndex.FileRecordSize];
            BinaryPrimitives.WriteUInt16LittleEndian(entry, (ushort)file.Kind);
            BinaryPrimitives.WriteUInt64LittleEndian(entry.AsSpan(2), file.Size);
            BinaryPrimitives.WriteUInt32LittleEndian(entry.AsSpan(10), offsets[file.RelativePath]);
            file.Sha256.Span.CopyTo(entry.AsSpan(14));
            destination.Write(entry);
        }
        destination.Flush();
    }
}
