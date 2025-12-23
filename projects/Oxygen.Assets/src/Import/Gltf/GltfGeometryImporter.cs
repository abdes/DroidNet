// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Numerics;
using System.Security.Cryptography;
using System.Text.Json;
using Oxygen.Assets.Import.Geometry;
using Oxygen.Assets.Persistence.LooseCooked.V1;
using SharpGLTF.Memory;
using SharpGLTF.Schema2;

namespace Oxygen.Assets.Import.Gltf;

/// <summary>
/// Imports glTF 2.0 sources (<c>.gltf</c>/<c>.glb</c>) and emits one or more cooked geometry assets (<c>.ogeo</c>)
/// as canonical imported geometry payloads.
/// </summary>
public sealed class GltfGeometryImporter : IAssetImporter
{
    private const string ImporterName = "Oxygen.Import.GltfGeometry";

    private static readonly ReadSettings ReadSettings = new();

    public string Name => ImporterName;

    public int Priority => 0;

    public bool CanImport(ImportProbe probe)
    {
        ArgumentNullException.ThrowIfNull(probe);

        return probe.Extension.Equals(".glb", StringComparison.OrdinalIgnoreCase)
            || probe.Extension.Equals(".gltf", StringComparison.OrdinalIgnoreCase);
    }

    public async Task<IReadOnlyList<ImportedAsset>> ImportAsync(ImportContext context, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(context);

        var sourcePath = context.Input.SourcePath;
        var ext = Path.GetExtension(sourcePath);
        if (!ext.Equals(".glb", StringComparison.OrdinalIgnoreCase) && !ext.Equals(".gltf", StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidOperationException($"{nameof(GltfGeometryImporter)} cannot import '{sourcePath}'.");
        }

        var meta = await context.Files.GetMetadataAsync(sourcePath, cancellationToken).ConfigureAwait(false);
        var sourceBytes = await context.Files.ReadAllBytesAsync(sourcePath, cancellationToken).ConfigureAwait(false);

        var model = ext.Equals(".glb", StringComparison.OrdinalIgnoreCase)
            ? TryLoadGlb(context, sourceBytes)
            : await TryLoadGltfAsync(context, sourcePath, sourceBytes, cancellationToken).ConfigureAwait(false);

        if (model is null)
        {
            return [];
        }

        var sourceHash = SHA256.HashData(sourceBytes.Span);
        var dependencies = DiscoverDependencies(context, ext, sourceBytes);

        var outputs = CreateOutputs(context, model, sourcePath, meta.LastWriteTimeUtc, sourceHash, dependencies, cancellationToken);

        return outputs;
    }

    private static ModelRoot? TryLoadGlb(ImportContext context, ReadOnlyMemory<byte> sourceBytes)
    {
        try
        {
            using var ms = new MemoryStream(sourceBytes.ToArray(), writable: false);
            return ModelRoot.ReadGLB(ms, ReadSettings);
        }
        catch (Exception ex)
        {
            context.Diagnostics.Add(
                ImportDiagnosticSeverity.Error,
                code: "OXYIMPORT_GLB_PARSE_FAILED",
                message: ex.Message,
                sourcePath: context.Input.SourcePath);

            if (context.Options.FailFast)
            {
                throw;
            }

            return null;
        }
    }

    private static async Task<ModelRoot?> TryLoadGltfAsync(
        ImportContext context,
        string sourcePath,
        ReadOnlyMemory<byte> gltfJsonUtf8,
        CancellationToken cancellationToken)
    {
        var workspace = new TempGltfWorkspace(sourcePath);
        try
        {
            await workspace.WriteGltfAsync(gltfJsonUtf8, cancellationToken).ConfigureAwait(false);

            return !await TryMaterializeGltfExternalResourcesAsync(context, sourcePath, gltfJsonUtf8, workspace, cancellationToken).ConfigureAwait(false)
                ? null
                : ModelRoot.Load(workspace.GltfPath, ReadSettings);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or NotSupportedException or JsonException)
        {
            context.Diagnostics.Add(
                ImportDiagnosticSeverity.Error,
                code: "OXYIMPORT_GLTF_PARSE_FAILED",
                message: ex.Message,
                sourcePath: sourcePath);

            if (context.Options.FailFast)
            {
                throw;
            }

            return null;
        }
        finally
        {
            await workspace.DisposeAsync().ConfigureAwait(false);
        }
    }

    private static async Task<bool> TryMaterializeGltfExternalResourcesAsync(
        ImportContext context,
        string sourcePath,
        ReadOnlyMemory<byte> gltfJsonUtf8,
        TempGltfWorkspace workspace,
        CancellationToken cancellationToken)
    {
        var (safeRelativeUris, unsafeUris) = GltfImportHelpers.EnumerateGltfReferencedResources(gltfJsonUtf8);

        if (unsafeUris.Count > 0)
        {
            var uri = unsafeUris[0];
            context.Diagnostics.Add(
                ImportDiagnosticSeverity.Error,
                code: "OXYIMPORT_GLTF_UNSAFE_URI",
                message: $"Unsupported or unsafe glTF uri '{uri}'.",
                sourcePath: sourcePath);

            return context.Options.FailFast
                ? throw new NotSupportedException($"Unsupported or unsafe glTF uri '{uri}'.")
                : false;
        }

        foreach (var safeRelative in safeRelativeUris)
        {
            cancellationToken.ThrowIfCancellationRequested();

            var resolvedSourceDep = Filesystem.VirtualPath.Normalize(GltfImportHelpers.ResolveRelativeToSource(sourcePath, safeRelative));
            if (!await workspace.TryWriteReferencedResourceToTempAsync(context, resolvedSourceDep, safeRelative, cancellationToken).ConfigureAwait(false))
            {
                return false;
            }
        }

        return true;
    }

    private static List<ImportedDependency> DiscoverDependencies(
        ImportContext context,
        string extension,
        ReadOnlyMemory<byte> sourceBytes)
    {
        var deps = new List<ImportedDependency>
        {
            new(context.Input.SourcePath, ImportedDependencyKind.SourceFile),
            new(GltfImportHelpers.GetSidecarPath(context.Input.SourcePath), ImportedDependencyKind.Sidecar),
        };

        if (extension.Equals(".gltf", StringComparison.OrdinalIgnoreCase))
        {
            // MVP: capture external buffer/image URIs from the JSON.
            var (safeRelativeUris, _) = GltfImportHelpers.EnumerateGltfReferencedResources(sourceBytes);
            foreach (var safeRelative in safeRelativeUris)
            {
                var path = Filesystem.VirtualPath.Normalize(GltfImportHelpers.ResolveRelativeToSource(context.Input.SourcePath, safeRelative));
                deps.Add(new ImportedDependency(path, ImportedDependencyKind.ReferencedResource));
            }
        }

        return [.. deps
            .DistinctBy(static d => (d.Path, d.Kind))
            .OrderBy(static d => d.Kind)
            .ThenBy(static d => d.Path, StringComparer.Ordinal),];
    }

    private static List<ImportedAsset> CreateOutputs(
        ImportContext context,
        ModelRoot model,
        string sourcePath,
        DateTimeOffset lastWriteTimeUtc,
        byte[] sourceHash,
        List<ImportedDependency> dependencies,
        CancellationToken cancellationToken)
    {
        var outputs = new List<ImportedAsset>();
        var dir = Filesystem.VirtualPath.GetDirectory(sourcePath);
        var stem = Filesystem.VirtualPath.GetFileNameWithoutExtension(sourcePath);

        for (var meshIndex = 0; meshIndex < model.LogicalMeshes.Count; meshIndex++)
        {
            cancellationToken.ThrowIfCancellationRequested();

            var mesh = model.LogicalMeshes[meshIndex];
            if (mesh.Primitives.Count == 0)
            {
                continue;
            }

            var geometry = BuildGeometry(mesh);
            var virtualPath = "/" + Filesystem.VirtualPath.Combine(dir, string.Create(CultureInfo.InvariantCulture, $"{stem}__mesh__{meshIndex:0000}.ogeo"));

            var assetKey = context.Identity.GetOrCreateAssetKey(virtualPath, assetType: "Geometry");

            outputs.Add(
                new ImportedAsset(
                    AssetKey: assetKey,
                    VirtualPath: virtualPath,
                    AssetType: "Geometry",
                    Source: new ImportedAssetSource(
                        SourcePath: sourcePath,
                        SourceHashSha256: sourceHash,
                        LastWriteTimeUtc: lastWriteTimeUtc),
                    Dependencies: dependencies,
                    Payload: geometry));
        }

        if (outputs.Count == 0)
        {
            context.Diagnostics.Add(
                ImportDiagnosticSeverity.Warning,
                code: "OXYIMPORT_GLTF_NO_MESHES",
                message: $"glTF contains no meshes with primitives: '{sourcePath}'.",
                sourcePath: sourcePath);
        }

        return outputs;
    }

    private static ImportedGeometry BuildGeometry(SharpGLTF.Schema2.Mesh mesh)
    {
        var vertices = new List<ImportedVertex>();
        var indices = new List<uint>();
        var subMeshes = new List<ImportedSubMesh>();

        var boundsMin = new Vector3(float.PositiveInfinity, float.PositiveInfinity, float.PositiveInfinity);
        var boundsMax = new Vector3(float.NegativeInfinity, float.NegativeInfinity, float.NegativeInfinity);

        for (var primIndex = 0; primIndex < mesh.Primitives.Count; primIndex++)
        {
            var prim = mesh.Primitives[primIndex];

            var firstVertex = (uint)vertices.Count;
            var primVertices = ReadVertices(prim);
            vertices.AddRange(primVertices);

            var firstIndex = (uint)indices.Count;
            var primIndices = ReadIndices(prim, vertexOffset: firstVertex);
            indices.AddRange(primIndices);

            // Fallback: compute from vertices
            var (pMin, pMax) = ComputeBoundsFromVertices(primVertices);

            boundsMin = Vector3.Min(boundsMin, pMin);
            boundsMax = Vector3.Max(boundsMax, pMax);

            subMeshes.Add(
                new ImportedSubMesh(
                    Name: string.Create(CultureInfo.InvariantCulture, $"Prim{primIndex:0000}"),
                    MaterialAssetKey: new AssetKey(0, 0),
                    FirstIndex: firstIndex,
                    IndexCount: (uint)primIndices.Count,
                    FirstVertex: firstVertex,
                    VertexCount: (uint)primVertices.Count,
                    Bounds: new ImportedBounds(pMin, pMax)));
        }

        if (vertices.Count == 0)
        {
            boundsMin = Vector3.Zero;
            boundsMax = Vector3.Zero;
        }

        return new ImportedGeometry(
            Name: string.IsNullOrWhiteSpace(mesh.Name) ? "Mesh" : mesh.Name,
            Vertices: vertices,
            Indices: indices,
            SubMeshes: subMeshes,
            Bounds: new ImportedBounds(boundsMin, boundsMax));
    }

    private static List<ImportedVertex> ReadVertices(SharpGLTF.Schema2.MeshPrimitive prim)
    {
        // SharpGLTF gives strongly typed accessors.
        var positions = prim.GetVertexAccessor("POSITION").AsVector3Array();

        var normals = TryGetVector3Array(prim, "NORMAL");
        var uvs = TryGetVector2Array(prim, "TEXCOORD_0");
        var tangents = TryGetVector4Array(prim, "TANGENT");

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

                // Bitangent = (N x T) * w
                bitan = Vector3.Cross(Vector3.Normalize(nrm), tan) * t4.W;
            }

            list.Add(
                new ImportedVertex(
                    Position: pos,
                    Normal: Vector3.Normalize(nrm),
                    Texcoord: uv,
                    Tangent: tan,
                    Bitangent: bitan,
                    Color: Vector4.One));
        }

        return list;
    }

    private static IAccessorArray<Vector3>? TryGetVector3Array(SharpGLTF.Schema2.MeshPrimitive prim, string attribute)
    {
        try
        {
            var accessor = prim.GetVertexAccessor(attribute);
            return accessor?.AsVector3Array();
        }
        catch (KeyNotFoundException)
        {
            return null;
        }
        catch (ArgumentException)
        {
            return null;
        }
    }

    private static IAccessorArray<Vector2>? TryGetVector2Array(SharpGLTF.Schema2.MeshPrimitive prim, string attribute)
    {
        try
        {
            var accessor = prim.GetVertexAccessor(attribute);
            return accessor?.AsVector2Array();
        }
        catch (KeyNotFoundException)
        {
            return null;
        }
        catch (ArgumentException)
        {
            return null;
        }
    }

    private static IAccessorArray<Vector4>? TryGetVector4Array(SharpGLTF.Schema2.MeshPrimitive prim, string attribute)
    {
        try
        {
            var accessor = prim.GetVertexAccessor(attribute);
            return accessor?.AsVector4Array();
        }
        catch (KeyNotFoundException)
        {
            return null;
        }
        catch (ArgumentException)
        {
            return null;
        }
    }

    private static List<uint> ReadIndices(SharpGLTF.Schema2.MeshPrimitive prim, uint vertexOffset)
    {
        var accessor = prim.IndexAccessor;
        if (accessor is null)
        {
            // Non-indexed primitive.
            var count = prim.GetVertexAccessor("POSITION").Count;
            return [.. Enumerable.Range(0, count).Select(i => (uint)i + vertexOffset)];
        }

        var idx = accessor.AsIndicesArray();
        var list = new List<uint>(idx.Count);
        for (var i = 0; i < idx.Count; i++)
        {
            list.Add((uint)idx[i] + vertexOffset);
        }

        return list;
    }

    private static (Vector3 min, Vector3 max) ComputeBoundsFromVertices(List<ImportedVertex> vertices)
    {
        if (vertices.Count == 0)
        {
            return (Vector3.Zero, Vector3.Zero);
        }

        var min = new Vector3(float.PositiveInfinity, float.PositiveInfinity, float.PositiveInfinity);
        var max = new Vector3(float.NegativeInfinity, float.NegativeInfinity, float.NegativeInfinity);

        foreach (var v in vertices)
        {
            min = Vector3.Min(min, v.Position);
            max = Vector3.Max(max, v.Position);
        }

        return (min, max);
    }
}
