// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers.Binary;
using System.Numerics;
using System.Text;
using Oxygen.Assets.Import.Geometry;

namespace Oxygen.Assets.Cook;

/// <summary>
/// Writes cooked <c>.ogeo</c> binary descriptors compatible with the runtime <c>GeometryAssetDesc</c>.
/// </summary>
public static class CookedGeometryWriter
{
    private const int DescriptorSize = 256;

    private const byte AssetTypeGeometry = 2;
    private const byte MeshTypeStandard = 1;

    private const int HeaderAssetTypeOffset = 0x00;
    private const int HeaderNameOffset = 0x01;
    private const int HeaderNameSize = 64;
    private const int HeaderVersionOffset = 0x41;
    private const int HeaderStreamingPriorityOffset = 0x42;
    private const int HeaderContentHashOffset = 0x43;
    private const int HeaderVariantFlagsOffset = 0x4B;

    private const int LodCountOffset = 0x5F;
    private const int BoundingBoxMinOffset = 0x63;
    private const int BoundingBoxMaxOffset = 0x6F;

    private const int MeshDescSize = 105;
    private const int SubMeshDescSize = 108;
    private const int MeshViewDescSize = 16;

    /// <summary>
    /// Writes a cooked <c>.ogeo</c> descriptor for a single geometry asset containing one LOD.
    /// </summary>
    public static void Write(
        Stream output,
        ImportedGeometry geometry,
        uint vertexBufferIndex,
        uint indexBufferIndex)
    {
        ArgumentNullException.ThrowIfNull(output);
        ArgumentNullException.ThrowIfNull(geometry);

        Span<byte> desc = stackalloc byte[DescriptorSize];
        desc.Clear();

        // AssetHeader
        desc[HeaderAssetTypeOffset] = AssetTypeGeometry;
        WriteName(desc.Slice(HeaderNameOffset, HeaderNameSize), string.IsNullOrWhiteSpace(geometry.Name) ? "Geometry" : geometry.Name);
        desc[HeaderVersionOffset] = 1;
        desc[HeaderStreamingPriorityOffset] = 0;
        BinaryPrimitives.WriteUInt64LittleEndian(desc.Slice(HeaderContentHashOffset, 8), 0);
        BinaryPrimitives.WriteUInt32LittleEndian(desc.Slice(HeaderVariantFlagsOffset, 4), 0);

        // GeometryAssetDesc
        BinaryPrimitives.WriteUInt32LittleEndian(desc.Slice(LodCountOffset, 4), 1);
        WriteVector3(desc.Slice(BoundingBoxMinOffset, 12), geometry.Bounds.Min);
        WriteVector3(desc.Slice(BoundingBoxMaxOffset, 12), geometry.Bounds.Max);

        output.Write(desc);

        WriteMeshDesc(output, geometry, vertexBufferIndex, indexBufferIndex);
        WriteSubMeshes(output, geometry);
    }

    private static void WriteMeshDesc(
        Stream output,
        ImportedGeometry geometry,
        uint vertexBufferIndex,
        uint indexBufferIndex)
    {
        Span<byte> mesh = stackalloc byte[MeshDescSize];
        mesh.Clear();

        // MeshDesc.name[64]
        WriteName(mesh[..64], string.IsNullOrWhiteSpace(geometry.Name) ? "Mesh" : geometry.Name);

        // MeshDesc.mesh_type
        mesh[64] = MeshTypeStandard;

        var submeshCount = (uint)(geometry.SubMeshes?.Count ?? 0);
        var meshViewCount = submeshCount; // MVP: 1 mesh view per submesh.

        // MeshDesc.submesh_count + mesh_view_count
        BinaryPrimitives.WriteUInt32LittleEndian(mesh.Slice(65, 4), submeshCount);
        BinaryPrimitives.WriteUInt32LittleEndian(mesh.Slice(69, 4), meshViewCount);

        // MeshDesc.info.standard
        BinaryPrimitives.WriteUInt32LittleEndian(mesh.Slice(73, 4), vertexBufferIndex);
        BinaryPrimitives.WriteUInt32LittleEndian(mesh.Slice(77, 4), indexBufferIndex);
        WriteVector3(mesh.Slice(81, 12), geometry.Bounds.Min);
        WriteVector3(mesh.Slice(93, 12), geometry.Bounds.Max);

        output.Write(mesh);
    }

    private static void WriteSubMeshes(Stream output, ImportedGeometry geometry)
    {
        if (geometry.SubMeshes is null || geometry.SubMeshes.Count == 0)
        {
            return;
        }

        Span<byte> subMesh = stackalloc byte[SubMeshDescSize];

        foreach (var sub in geometry.SubMeshes)
        {
            subMesh.Clear();

            // SubMeshDesc.name[64]
            WriteName(subMesh[..64], string.IsNullOrWhiteSpace(sub.Name) ? "SubMesh" : sub.Name);

            // SubMeshDesc.material_asset_key (16 bytes)
            sub.MaterialAssetKey.WriteBytes(subMesh.Slice(64, 16));

            // SubMeshDesc.mesh_view_count
            BinaryPrimitives.WriteUInt32LittleEndian(subMesh.Slice(80, 4), 1);

            // SubMeshDesc.bounding_box_* (MVP uses provided bounds)
            WriteVector3(subMesh.Slice(84, 12), sub.Bounds.Min);
            WriteVector3(subMesh.Slice(96, 12), sub.Bounds.Max);

            output.Write(subMesh);
            WriteMeshView(output, sub);
        }
    }

    private static void WriteMeshView(Stream output, ImportedSubMesh sub)
    {
        Span<byte> view = stackalloc byte[MeshViewDescSize];
        view.Clear();

        BinaryPrimitives.WriteUInt32LittleEndian(view[..4], sub.FirstIndex);
        BinaryPrimitives.WriteUInt32LittleEndian(view.Slice(4, 4), sub.IndexCount);
        BinaryPrimitives.WriteUInt32LittleEndian(view.Slice(8, 4), sub.FirstVertex);
        BinaryPrimitives.WriteUInt32LittleEndian(view.Slice(12, 4), sub.VertexCount);

        output.Write(view);
    }

    private static void WriteName(Span<byte> destination, string name)
    {
        destination.Clear();

        if (string.IsNullOrEmpty(name))
        {
            return;
        }

        var bytes = Encoding.UTF8.GetBytes(name);
        var count = Math.Min(bytes.Length, destination.Length - 1);
        bytes.AsSpan(0, count).CopyTo(destination);
        destination[count] = 0;
    }

    private static void WriteVector3(Span<byte> destination, Vector3 value)
    {
        WriteSingle(destination[..4], value.X);
        WriteSingle(destination.Slice(4, 4), value.Y);
        WriteSingle(destination.Slice(8, 4), value.Z);
    }

    private static void WriteSingle(Span<byte> destination, float value)
        => BinaryPrimitives.WriteUInt32LittleEndian(destination, (uint)BitConverter.SingleToInt32Bits(value));
}
