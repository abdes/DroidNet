// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Text.Json;
using Oxygen.Assets.Import;
using Oxygen.Assets.Import.Geometry;
using Oxygen.Assets.Import.Materials;
using Oxygen.Assets.Import.Scenes;
using Oxygen.Assets.Import.Textures;
using SharpGLTF.Schema2;

namespace Oxygen.Assets.Cook;

internal sealed class IntermediateAssetLoader
{
    private static readonly JsonSerializerOptions JsonOptions = new() { PropertyNameCaseInsensitive = true };

    private readonly IImportFileAccess fileAccess;

    public IntermediateAssetLoader(IImportFileAccess fileAccess)
    {
        this.fileAccess = fileAccess;
    }

    public async Task<MaterialSource?> LoadMaterialAsync(ImportedAsset asset, CancellationToken ct)
    {
        if (asset.Payload is MaterialSource ms)
        {
            return ms;
        }

        if (string.IsNullOrEmpty(asset.GeneratedSourcePath))
        {
            return null;
        }

        try
        {
            var bytes = await this.fileAccess.ReadAllBytesAsync(asset.GeneratedSourcePath, ct).ConfigureAwait(false);
            return JsonSerializer.Deserialize<MaterialSource>(bytes.Span, JsonOptions);
        }
        catch (FileNotFoundException)
        {
            return null;
        }
    }

    public async Task<SceneSource?> LoadSceneAsync(ImportedAsset asset, CancellationToken ct)
    {
        if (asset.Payload is SceneSource ss)
        {
            return ss;
        }

        if (string.IsNullOrEmpty(asset.GeneratedSourcePath))
        {
            return null;
        }

        try
        {
            var bytes = await this.fileAccess.ReadAllBytesAsync(asset.GeneratedSourcePath, ct).ConfigureAwait(false);
            return JsonSerializer.Deserialize<SceneSource>(bytes.Span, JsonOptions);
        }
        catch (FileNotFoundException)
        {
            return null;
        }
    }

    public async Task<GeometryCookInput?> LoadGeometryAsync(ImportedAsset asset, CancellationToken ct)
    {
        if (asset.Payload is GeometryCookInput gci)
        {
            return gci;
        }

        if (string.IsNullOrEmpty(asset.IntermediateCachePath) || string.IsNullOrEmpty(asset.GeneratedSourcePath))
        {
            throw new InvalidDataException($"[IntermediateAssetLoader] Missing paths for {asset.VirtualPath}. Gen: '{asset.GeneratedSourcePath}', Cache: '{asset.IntermediateCachePath}'");
        }

        try
        {
            // 1. Load Metadata
            var metaBytes = await this.fileAccess.ReadAllBytesAsync(asset.GeneratedSourcePath, ct).ConfigureAwait(false);
            var metadata = JsonSerializer.Deserialize<ImportedGeometry>(metaBytes.Span, JsonOptions);
            if (metadata == null)
            {
                return null;
            }

            // 2. Load GLB
            var glbBytes = await this.fileAccess.ReadAllBytesAsync(asset.IntermediateCachePath, ct).ConfigureAwait(false);
            using var ms = new MemoryStream(glbBytes.ToArray(), writable: false);
            var model = ModelRoot.ReadGLB(ms);
            var mesh = model.LogicalMeshes.Count > 0 ? model.LogicalMeshes[0] : null;
            if (mesh == null)
            {
                return null;
            }

            // 3. Reconstruct
            var data = LoadGeometryData(mesh, metadata);
            return new GeometryCookInput(asset.AssetKey, metadata, data);
        }
        catch (Exception)
        {
            throw;
        }
    }

    public async Task<byte[]?> LoadTextureBytesAsync(ImportedAsset asset, CancellationToken ct)
    {
        // For textures, this returns the raw bytes of the intermediate payload snapshot.
        if (string.IsNullOrEmpty(asset.IntermediateCachePath))
        {
            return null;
        }

        try
        {
            var memory = await this.fileAccess.ReadAllBytesAsync(asset.IntermediateCachePath, ct).ConfigureAwait(false);
            return memory.ToArray();
        }
        catch (FileNotFoundException)
        {
            return null;
        }
    }

    public async Task<TextureSourceData?> LoadTextureSourceAsync(ImportedAsset asset, CancellationToken ct)
    {
        if (asset.Payload is TextureSourceData tsd)
        {
            return tsd;
        }

        if (string.IsNullOrEmpty(asset.GeneratedSourcePath))
        {
            return null;
        }

        try
        {
            var bytes = await this.fileAccess.ReadAllBytesAsync(asset.GeneratedSourcePath, ct).ConfigureAwait(false);
            return TextureSourceReader.Read(bytes.Span);
        }
        catch (FileNotFoundException)
        {
            return null;
        }
    }

    private static GeometryData LoadGeometryData(SharpGLTF.Schema2.Mesh mesh, ImportedGeometry metadata)
    {
        var vertices = new List<ImportedVertex>();
        var indices = new List<uint>();

        // We assume the primitives in the GLB match the order of SubMeshes in metadata
        for (var i = 0; i < mesh.Primitives.Count && i < metadata.SubMeshes.Count; i++)
        {
            var prim = mesh.Primitives[i];

            var firstVertex = (uint)vertices.Count;
            var primVertices = ReadVertices(prim);
            vertices.AddRange(primVertices);

            var primIndices = ReadIndices(prim, vertexOffset: firstVertex);
            indices.AddRange(primIndices);
        }

        return new GeometryData(vertices, indices);
    }

    private static List<ImportedVertex> ReadVertices(SharpGLTF.Schema2.MeshPrimitive prim)
    {
        var positions = prim.GetVertexAccessor("POSITION").AsVector3Array();
        var normals = prim.GetVertexAccessor("NORMAL")?.AsVector3Array();
        var uvs = prim.GetVertexAccessor("TEXCOORD_0")?.AsVector2Array();
        var tangents = prim.GetVertexAccessor("TANGENT")?.AsVector4Array();

        var list = new List<ImportedVertex>(positions.Count);

        for (var i = 0; i < positions.Count; i++)
        {
            var pos = positions[i];
            var nrm = normals is null ? Vector3.UnitY : normals[i];
            var uv = uvs is null ? Vector2.Zero : uvs[i];

            var tan = Vector3.Zero;
            var bitan = Vector3.Zero;

            if (tangents != null)
            {
                var t4 = tangents[i];
                tan = new Vector3(t4.X, t4.Y, t4.Z);
                bitan = Vector3.Cross(Vector3.Normalize(nrm), tan) * t4.W;
            }

            list.Add(new ImportedVertex(
                Position: pos,
                Normal: Vector3.Normalize(nrm),
                Texcoord: uv,
                Tangent: tan,
                Bitangent: bitan,
                Color: Vector4.One));
        }

        return list;
    }

    private static List<uint> ReadIndices(SharpGLTF.Schema2.MeshPrimitive prim, uint vertexOffset)
    {
        var indices = prim.GetIndices();
        var list = new List<uint>(indices.Count);
        for (var i = 0; i < indices.Count; i++)
        {
            list.Add((uint)indices[i] + vertexOffset);
        }

        return list;
    }
}
