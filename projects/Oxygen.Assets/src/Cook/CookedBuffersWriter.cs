// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers.Binary;
using System.Numerics;
using Oxygen.Assets.Import.Geometry;
using Oxygen.Assets.Persistence.LooseCooked.V1;

namespace Oxygen.Assets.Cook;

/// <summary>
/// Writes <c>buffers.table</c> and <c>buffers.data</c> for a loose cooked mount.
/// </summary>
public static class CookedBuffersWriter
{
    private const int BufferResourceDescSize = 32;

    private const uint UsageVertexBuffer = 0x01;
    private const uint UsageIndexBuffer = 0x02;
    private const uint UsageStatic = 0x100;

    public static CookedBuffersResult Write(IReadOnlyList<GeometryCookInput> geometries)
    {
        ArgumentNullException.ThrowIfNull(geometries);

        // Resource index 0 is reserved.
        var mapping = new Dictionary<AssetKey, GeometryBufferPair>(geometries.Count);

        using var dataStream = new MemoryStream();
        var tableEntries = new List<byte[]> { new byte[BufferResourceDescSize] };

        foreach (var item in geometries)
        {
            if (item.Geometry is null)
            {
                continue;
            }

            var assetKey = item.AssetKey;
            var geometry = item.Geometry;

            var vertexIndex = (uint)tableEntries.Count;
            var vertexBytes = SerializeVertices(geometry.Vertices);
            var vertexOffset = AppendAligned(dataStream, vertexBytes, alignment: 72);
            tableEntries.Add(MakeBufferDesc(
                dataOffset: vertexOffset,
                sizeBytes: (uint)vertexBytes.Length,
                usageFlags: UsageVertexBuffer | UsageStatic,
                elementStride: 72,
                elementFormat: 0));

            var indexIndex = (uint)tableEntries.Count;
            var indexBytes = SerializeUInt32(geometry.Indices);
            var indexOffset = AppendAligned(dataStream, indexBytes, alignment: 4);
            tableEntries.Add(MakeBufferDesc(
                dataOffset: indexOffset,
                sizeBytes: (uint)indexBytes.Length,
                usageFlags: UsageIndexBuffer | UsageStatic,
                elementStride: 4,
                elementFormat: 0));

            mapping[assetKey] = new GeometryBufferPair(vertexIndex, indexIndex);
        }

        var tableBytes = new byte[checked(tableEntries.Count * BufferResourceDescSize)];
        for (var i = 0; i < tableEntries.Count; i++)
        {
            tableEntries[i].CopyTo(tableBytes, i * BufferResourceDescSize);
        }

        return new CookedBuffersResult(
            BuffersTableBytes: tableBytes,
            BuffersDataBytes: dataStream.ToArray(),
            BufferIndices: mapping);
    }

    private static ulong AppendAligned(MemoryStream stream, ReadOnlySpan<byte> bytes, int alignment)
    {
        ArgumentNullException.ThrowIfNull(stream);
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(alignment);

        var pos = (ulong)stream.Position;
        var alignmentU = (ulong)alignment;
        var padU = (alignmentU - (pos % alignmentU)) % alignmentU;
        var pad = (int)padU;
        if (pad != 0)
        {
            stream.Write(new byte[pad]);
            pos += (uint)pad;
        }

        stream.Write(bytes);
        return pos;
    }

    private static byte[] MakeBufferDesc(
        ulong dataOffset,
        uint sizeBytes,
        uint usageFlags,
        uint elementStride,
        byte elementFormat)
    {
        var desc = new byte[BufferResourceDescSize];
        var span = desc.AsSpan();

        BinaryPrimitives.WriteUInt64LittleEndian(span[..8], dataOffset);
        BinaryPrimitives.WriteUInt32LittleEndian(span.Slice(8, 4), sizeBytes);
        BinaryPrimitives.WriteUInt32LittleEndian(span.Slice(12, 4), usageFlags);
        BinaryPrimitives.WriteUInt32LittleEndian(span.Slice(16, 4), elementStride);
        span[20] = elementFormat;

        // Reserved bytes remain zero.
        return desc;
    }

    private static byte[] SerializeVertices(IReadOnlyList<ImportedVertex> vertices)
    {
        ArgumentNullException.ThrowIfNull(vertices);

        var bytes = new byte[checked(vertices.Count * 72)];
        var span = bytes.AsSpan();

        for (var i = 0; i < vertices.Count; i++)
        {
            var v = vertices[i];
            var o = i * 72;

            WriteVector3(span.Slice(o + 0, 12), v.Position);
            WriteVector3(span.Slice(o + 12, 12), v.Normal);
            WriteVector2(span.Slice(o + 24, 8), v.Texcoord);
            WriteVector3(span.Slice(o + 32, 12), v.Tangent);
            WriteVector3(span.Slice(o + 44, 12), v.Bitangent);
            WriteVector4(span.Slice(o + 56, 16), v.Color);
        }

        return bytes;
    }

    private static byte[] SerializeUInt32(IReadOnlyList<uint> values)
    {
        ArgumentNullException.ThrowIfNull(values);

        var bytes = new byte[checked(values.Count * 4)];
        var span = bytes.AsSpan();

        for (var i = 0; i < values.Count; i++)
        {
            BinaryPrimitives.WriteUInt32LittleEndian(span.Slice(i * 4, 4), values[i]);
        }

        return bytes;
    }

    private static void WriteVector2(Span<byte> destination, Vector2 value)
    {
        WriteSingle(destination[..4], value.X);
        WriteSingle(destination.Slice(4, 4), value.Y);
    }

    private static void WriteVector3(Span<byte> destination, Vector3 value)
    {
        WriteSingle(destination[..4], value.X);
        WriteSingle(destination.Slice(4, 4), value.Y);
        WriteSingle(destination.Slice(8, 4), value.Z);
    }

    private static void WriteVector4(Span<byte> destination, Vector4 value)
    {
        WriteSingle(destination[..4], value.X);
        WriteSingle(destination.Slice(4, 4), value.Y);
        WriteSingle(destination.Slice(8, 4), value.Z);
        WriteSingle(destination.Slice(12, 4), value.W);
    }

    private static void WriteSingle(Span<byte> destination, float value)
        => BinaryPrimitives.WriteUInt32LittleEndian(destination, (uint)BitConverter.SingleToInt32Bits(value));
}
