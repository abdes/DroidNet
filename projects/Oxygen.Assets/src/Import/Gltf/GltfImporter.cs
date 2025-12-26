// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Numerics;
using System.Security.Cryptography;
using System.Text.Json;
using Oxygen.Assets.Import.Geometry;
using Oxygen.Assets.Import.Materials;
using Oxygen.Assets.Import.Scenes;
using Oxygen.Assets.Import.Textures;
using Oxygen.Assets.Persistence.LooseCooked.V1;
using SharpGLTF.Geometry;
using SharpGLTF.Geometry.VertexTypes;
using SharpGLTF.Memory;
using SharpGLTF.Scenes;
using SharpGLTF.Schema2;
using SixLabors.ImageSharp;

namespace Oxygen.Assets.Import.Gltf;

/// <summary>
/// Imports glTF 2.0 sources (<c>.gltf</c>/<c>.glb</c>) and emits one or more cooked geometry assets (<c>.ogeo</c>)
/// as canonical imported geometry payloads.
/// </summary>
public sealed class GltfImporter : IAssetImporter
{
    private const string ImporterName = "Oxygen.Import.GltfGeometry";

    private static readonly ReadSettings ReadSettings = new();
    private static readonly JsonSerializerOptions JsonOptions = new() { WriteIndented = true };

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
            throw new InvalidOperationException($"{nameof(GltfImporter)} cannot import '{sourcePath}'.");
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
        var settings = await ImportSettingsHelper.GetEffectiveSettingsAsync(context, cancellationToken).ConfigureAwait(false);

        var lastWriteTimeUtc = meta.LastWriteTimeUtc;
        var outputs = await CreateOutputsAsync(context, model, sourcePath, lastWriteTimeUtc, sourceHash, dependencies, settings, cancellationToken).ConfigureAwait(false);

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

    private static IReadOnlyList<ImportedDependency> CreateAssetDependencies(
        List<ImportedDependency> baseDependencies,
        string generatedSourcePath)
    {
        return [.. baseDependencies
            .Append(new ImportedDependency(generatedSourcePath, ImportedDependencyKind.SourceFile))
            .DistinctBy(static d => (d.Path, d.Kind))
            .OrderBy(static d => d.Kind)
            .ThenBy(static d => d.Path, StringComparer.Ordinal),];
    }

    private static async Task<List<ImportedAsset>> CreateOutputsAsync(
        ImportContext context,
        ModelRoot model,
        string sourcePath,
        DateTimeOffset lastWriteTimeUtc,
        byte[] sourceHash,
        List<ImportedDependency> dependencies,
        Dictionary<string, string> settings,
        CancellationToken cancellationToken)
    {
        var outputs = new List<ImportedAsset>();
        var dir = Filesystem.VirtualPath.GetDirectory(sourcePath);
        var stem = Filesystem.VirtualPath.GetFileNameWithoutExtension(sourcePath);

        var texturePaths = await ExtractTexturesAsync(context, model, sourcePath, lastWriteTimeUtc, sourceHash, dependencies, outputs, dir, stem, cancellationToken).ConfigureAwait(false);

        var materialKeys = await ExtractMaterialsAsync(context, model, sourcePath, lastWriteTimeUtc, sourceHash, dependencies, outputs, dir, stem, texturePaths, settings, cancellationToken).ConfigureAwait(false);

        var meshPaths = await ExtractMeshesAsync(context, model, sourcePath, lastWriteTimeUtc, sourceHash, dependencies, outputs, dir, stem, materialKeys, cancellationToken).ConfigureAwait(false);

        await ExtractScenesAsync(context, model, sourcePath, lastWriteTimeUtc, sourceHash, dependencies, outputs, dir, stem, meshPaths, cancellationToken).ConfigureAwait(false);

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

    private static async Task<Dictionary<int, string>> ExtractTexturesAsync(
        ImportContext context,
        ModelRoot model,
        string sourcePath,
        DateTimeOffset lastWriteTimeUtc,
        byte[] sourceHash,
        List<ImportedDependency> dependencies,
        List<ImportedAsset> outputs,
        string dir,
        string stem,
        CancellationToken cancellationToken)
    {
        var texturePaths = new Dictionary<int, string>();

        for (var i = 0; i < model.LogicalTextures.Count; i++)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var tex = model.LogicalTextures[i];
            var img = tex.PrimaryImage;

            if (img.Content.IsEmpty)
            {
                continue;
            }

            var texName = string.Create(CultureInfo.InvariantCulture, $"{stem}__texture__{i:0000}");
            var virtualPath = "/" + Filesystem.VirtualPath.Combine(dir, texName + ".otex");

            var relativePath = virtualPath.TrimStart('/');
            var pngRelativePath = Path.ChangeExtension(relativePath, AssetPipelineConstants.PngExtension);
            var intermediatePath = Path.Combine(AssetPipelineConstants.ImportedFolderName, pngRelativePath);

            int width;
            int height;
            using (var image = SixLabors.ImageSharp.Image.Load(img.Content.Content.Span))
            {
                width = image.Width;
                height = image.Height;

                var ms = new MemoryStream();
                await image.SaveAsPngAsync(ms, cancellationToken).ConfigureAwait(false);
                var bytes = ms.ToArray();
                await context.Files.WriteAllBytesAsync(intermediatePath, bytes, cancellationToken).ConfigureAwait(false);
            }

            // Generate a canonical texture authoring source that the build step can read.
            // For extracted textures, the source image is the extracted intermediate PNG.
            var data = new TextureSourceData(
                Schema: "oxygen.texture.v1",
                Name: tex.Name ?? texName,
                SourceImage: intermediatePath,
                ColorSpace: "Srgb",
                TextureType: "Texture2D",
                MipPolicy: new TextureMipPolicyData(Mode: "None"),
                RuntimeFormat: new TextureRuntimeFormatData(Format: "R8G8B8A8_UNorm", Compression: "None"),
                Imported: new TextureImportedData(width, height));

            var generatedSourcePath = virtualPath.TrimStart('/') + ".json";
            using var msOut = new MemoryStream();
            TextureSourceWriter.Write(msOut, data);
            await context.Files.WriteAllBytesAsync(generatedSourcePath, msOut.ToArray(), cancellationToken).ConfigureAwait(false);

            var assetKey = context.Identity.GetOrCreateAssetKey(virtualPath, assetType: "Texture");
            texturePaths[i] = virtualPath;

            outputs.Add(new ImportedAsset(
                AssetKey: assetKey,
                VirtualPath: virtualPath,
                AssetType: "Texture",
                Source: new ImportedAssetSource(
                    SourcePath: sourcePath,
                    SourceHashSha256: sourceHash,
                    LastWriteTimeUtc: lastWriteTimeUtc),
                Dependencies: CreateAssetDependencies(dependencies, generatedSourcePath),
                GeneratedSourcePath: generatedSourcePath,
                IntermediateCachePath: intermediatePath,
                Payload: null));
        }

        return texturePaths;
    }

    private static async Task<Dictionary<int, AssetKey>> ExtractMaterialsAsync(
        ImportContext context,
        ModelRoot model,
        string sourcePath,
        DateTimeOffset lastWriteTimeUtc,
        byte[] sourceHash,
        List<ImportedDependency> dependencies,
        List<ImportedAsset> outputs,
        string dir,
        string stem,
        Dictionary<int, string> texturePaths,
        Dictionary<string, string> settings,
        CancellationToken cancellationToken)
    {
        var materialDir = settings.TryGetValue("MaterialDestination", out var dest) ? dest : dir;

        var materialKeys = new Dictionary<int, AssetKey>();
        for (var i = 0; i < model.LogicalMaterials.Count; i++)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var mat = model.LogicalMaterials[i];

            var matName = string.Create(CultureInfo.InvariantCulture, $"{stem}__material__{i:0000}");
            var jsonPath = Filesystem.VirtualPath.Combine(materialDir, matName + ".omat.json");

            MaterialSource matSource;
            if (await ExistsAsync(context.Files, jsonPath, cancellationToken).ConfigureAwait(false))
            {
                // Preserve user edits: load existing source
                var jsonBytes = await context.Files.ReadAllBytesAsync(jsonPath, cancellationToken).ConfigureAwait(false);
                matSource = JsonSerializer.Deserialize<MaterialSource>(jsonBytes.Span, JsonOptions)!;
            }
            else
            {
                // Generate new source
                matSource = BuildMaterial(mat, texturePaths);
                var jsonBytes = JsonSerializer.SerializeToUtf8Bytes(matSource, JsonOptions);
                await context.Files.WriteAllBytesAsync(jsonPath, jsonBytes, cancellationToken).ConfigureAwait(false);
            }

            // Ensure the VirtualPath matches what MaterialSourceImporter produces (stripping .json).
            // Example: "Content/Materials/Foo.omat.json" -> "/Content/Materials/Foo.omat"
            var matVirtualPath = "/" + jsonPath[..^5];

            var matKey = context.Identity.GetOrCreateAssetKey(matVirtualPath, assetType: "Material");
            materialKeys[i] = matKey;

            outputs.Add(new ImportedAsset(
                AssetKey: matKey,
                VirtualPath: matVirtualPath,
                AssetType: "Material",
                Source: new ImportedAssetSource(
                    SourcePath: sourcePath,
                    SourceHashSha256: sourceHash,
                    LastWriteTimeUtc: lastWriteTimeUtc),
                Dependencies: CreateAssetDependencies(dependencies, jsonPath),
                GeneratedSourcePath: jsonPath,
                IntermediateCachePath: null,
                Payload: matSource));
        }

        return materialKeys;
    }

    private static async Task<Dictionary<int, string>> ExtractMeshesAsync(
        ImportContext context,
        ModelRoot model,
        string sourcePath,
        DateTimeOffset lastWriteTimeUtc,
        byte[] sourceHash,
        List<ImportedDependency> dependencies,
        List<ImportedAsset> outputs,
        string dir,
        string stem,
        Dictionary<int, AssetKey> materialKeys,
        CancellationToken cancellationToken)
    {
        var meshPaths = new Dictionary<int, string>();

        for (var meshIndex = 0; meshIndex < model.LogicalMeshes.Count; meshIndex++)
        {
            cancellationToken.ThrowIfCancellationRequested();

            var mesh = model.LogicalMeshes[meshIndex];
            if (mesh.Primitives.Count == 0)
            {
                continue;
            }

            var (geometry, geometryData) = BuildGeometry(mesh, sourcePath, meshIndex, materialKeys);
            var virtualPath = "/" + Filesystem.VirtualPath.Combine(dir, string.Create(CultureInfo.InvariantCulture, $"{stem}__mesh__{meshIndex:0000}.ogeo"));

            var relativePath = virtualPath.TrimStart('/');
            var glbRelativePath = Path.ChangeExtension(relativePath, AssetPipelineConstants.GlbExtension);
            var intermediatePath = Path.Combine(AssetPipelineConstants.ImportedFolderName, glbRelativePath);

            await WriteMeshToGlbAsync(context, intermediatePath, geometry, geometryData, cancellationToken).ConfigureAwait(false);

            var subMeshes = new List<ImportedSubMesh>(geometry.SubMeshes);

            var generatedSourcePath = virtualPath.TrimStart('/') + ".json";

            if (await ExistsAsync(context.Files, generatedSourcePath, cancellationToken).ConfigureAwait(false))
            {
                try
                {
                    var oldBytes = await context.Files.ReadAllBytesAsync(generatedSourcePath, cancellationToken).ConfigureAwait(false);
                    var oldMeta = JsonSerializer.Deserialize<ImportedGeometry>(oldBytes.Span, JsonOptions);
                    if (oldMeta != null)
                    {
                        for (var i = 0; i < subMeshes.Count && i < oldMeta.SubMeshes.Count; i++)
                        {
                            // Preserve user-assigned material
                            var oldSubMesh = oldMeta.SubMeshes[i];
                            var newSubMesh = subMeshes[i];

                            // We only copy the MaterialAssetKey, assuming the geometry structure (counts/offsets)
                            // comes from the fresh import.
                            subMeshes[i] = newSubMesh with { MaterialAssetKey = oldSubMesh.MaterialAssetKey };
                        }
                    }
                }
                catch (Exception)
                {
                    // Ignore invalid existing metadata
                }
            }

            var metadata = geometry with { SubMeshes = subMeshes };

            var jsonBytes = JsonSerializer.SerializeToUtf8Bytes(metadata, JsonOptions);
            await context.Files.WriteAllBytesAsync(generatedSourcePath, jsonBytes, cancellationToken).ConfigureAwait(false);

            var assetKey = context.Identity.GetOrCreateAssetKey(virtualPath, assetType: "Geometry");
            meshPaths[meshIndex] = virtualPath;

            outputs.Add(
                new ImportedAsset(
                    AssetKey: assetKey,
                    VirtualPath: virtualPath,
                    AssetType: "Geometry",
                    Source: new ImportedAssetSource(
                        SourcePath: sourcePath,
                        SourceHashSha256: sourceHash,
                        LastWriteTimeUtc: lastWriteTimeUtc),
                    Dependencies: CreateAssetDependencies(dependencies, generatedSourcePath),
                    GeneratedSourcePath: generatedSourcePath,
                    IntermediateCachePath: intermediatePath,
                    Payload: null));
        }

        return meshPaths;
    }

    private static async Task WriteMeshToGlbAsync(
        ImportContext context,
        string intermediatePath,
        ImportedGeometry geometry,
        GeometryData data,
        CancellationToken cancellationToken)
    {
        var meshBuilder = new MeshBuilder<VertexPositionNormal, VertexTexture1>(geometry.Name);

        foreach (var subMesh in geometry.SubMeshes)
        {
            var prim = meshBuilder.UsePrimitive(SharpGLTF.Materials.MaterialBuilder.CreateDefault());

            for (var i = 0; i < subMesh.IndexCount; i += 3)
            {
                var idx0 = data.Indices[(int)(subMesh.FirstIndex + i)];
                var idx1 = data.Indices[(int)(subMesh.FirstIndex + i + 1)];
                var idx2 = data.Indices[(int)(subMesh.FirstIndex + i + 2)];

                prim.AddTriangle(
                    ToGltfVertex(data.Vertices[(int)idx0]),
                    ToGltfVertex(data.Vertices[(int)idx1]),
                    ToGltfVertex(data.Vertices[(int)idx2]));
            }
        }

        var scene = new SceneBuilder();
        scene.AddRigidMesh(meshBuilder, Matrix4x4.Identity);
        var model = scene.ToGltf2();

        var ms = new MemoryStream();
        model.WriteGLB(ms);
        var bytes = ms.ToArray();

        await context.Files.WriteAllBytesAsync(intermediatePath, bytes, cancellationToken).ConfigureAwait(false);
    }

    private static async Task<bool> ExistsAsync(IImportFileAccess files, string path, CancellationToken cancellationToken)
    {
        try
        {
            await files.GetMetadataAsync(path, cancellationToken).ConfigureAwait(false);
            return true;
        }
        catch (FileNotFoundException)
        {
            return false;
        }
    }

    private static (VertexPositionNormal, VertexTexture1) ToGltfVertex(ImportedVertex v)
    {
        var vpn = new VertexPositionNormal(v.Position, v.Normal);
        var vt = new VertexTexture1(v.Texcoord);

        return (vpn, vt);
    }

    private static async Task ExtractScenesAsync(
        ImportContext context,
        ModelRoot model,
        string sourcePath,
        DateTimeOffset lastWriteTimeUtc,
        byte[] sourceHash,
        List<ImportedDependency> dependencies,
        List<ImportedAsset> outputs,
        string dir,
        string stem,
        Dictionary<int, string> meshPaths,
        CancellationToken cancellationToken)
    {
        var scene = model.DefaultScene;
        if (scene == null)
        {
            return;
        }

        var sceneName = string.Create(CultureInfo.InvariantCulture, $"{stem}__scene");
        var virtualPath = "/" + Filesystem.VirtualPath.Combine(dir, sceneName + AssetPipelineConstants.SceneExtension);

        var nodes = new List<SceneNodeSource>();
        foreach (var node in scene.VisualChildren)
        {
            nodes.Add(BuildSceneNode(node, meshPaths));
        }

        var sceneSource = new SceneSource(
            Schema: "oxygen.scene.v1",
            Name: scene.Name ?? sceneName,
            Nodes: nodes);

        var generatedSourcePath = virtualPath.TrimStart('/') + AssetPipelineConstants.GeneratedSourceExtension;
        var jsonBytes = JsonSerializer.SerializeToUtf8Bytes(sceneSource, JsonOptions);
        await context.Files.WriteAllBytesAsync(generatedSourcePath, jsonBytes, cancellationToken).ConfigureAwait(false);

        var assetKey = context.Identity.GetOrCreateAssetKey(virtualPath, assetType: "Scene");

        outputs.Add(new ImportedAsset(
            AssetKey: assetKey,
            VirtualPath: virtualPath,
            AssetType: "Scene",
            Source: new ImportedAssetSource(
                SourcePath: sourcePath,
                SourceHashSha256: sourceHash,
                LastWriteTimeUtc: lastWriteTimeUtc),
            Dependencies: CreateAssetDependencies(dependencies, generatedSourcePath),
            GeneratedSourcePath: generatedSourcePath,
            IntermediateCachePath: null,
            Payload: null));
    }

    private static SceneNodeSource BuildSceneNode(SharpGLTF.Schema2.Node node, Dictionary<int, string> meshPaths)
    {
        string? meshUri = null;
        if (node.Mesh != null)
        {
            if (meshPaths.TryGetValue(node.Mesh.LogicalIndex, out var meshPath))
            {
                meshUri = "asset://" + meshPath;
            }
        }

        // SharpGLTF nodes may express transforms either as SRT components or as a matrix.
        // Accessing Rotation/Scale/Translation on an AffineTransform that is not in SRT
        // representation throws. Decompose once and use the decomposed SRT.
        var local = node.LocalTransform.GetDecomposed();

        var translation = local.Translation;
        var rotation = local.Rotation;
        var scale = local.Scale;

        var children = new List<SceneNodeSource>();
        foreach (var child in node.VisualChildren)
        {
            children.Add(BuildSceneNode(child, meshPaths));
        }

        return new SceneNodeSource(
            Name: node.Name,
            Translation: translation == Vector3.Zero ? null : translation,
            Rotation: rotation == Quaternion.Identity ? null : rotation,
            Scale: scale == Vector3.One ? null : scale,
            Mesh: meshUri,
            Children: children.Count > 0 ? children : null);
    }

    private static MaterialSource BuildMaterial(SharpGLTF.Schema2.Material mat, Dictionary<int, string> texturePaths)
    {
        var baseColorChannel = mat.FindChannel("BaseColor");
        var baseColor = baseColorChannel?.Color ?? Vector4.One;
        var baseColorTexture = GetTextureSlot(baseColorChannel, texturePaths);

        var mrChannel = mat.FindChannel("MetallicRoughness");
        var metallic = (float)(mrChannel?.GetFactor("MetallicFactor") ?? 1.0);
        var roughness = (float)(mrChannel?.GetFactor("RoughnessFactor") ?? 1.0);
        var mrTexture = GetTextureSlot(mrChannel, texturePaths);

        var normalChannel = mat.FindChannel("Normal");
        var normalTexture = GetTextureSlot(normalChannel, texturePaths);

        var occlusionChannel = mat.FindChannel("Occlusion");
        var occlusionTexture = GetTextureSlot(occlusionChannel, texturePaths);

        var pbrData = new MaterialPbrMetallicRoughness(
            baseColorR: baseColor.X,
            baseColorG: baseColor.Y,
            baseColorB: baseColor.Z,
            baseColorA: baseColor.W,
            metallicFactor: metallic,
            roughnessFactor: roughness,
            baseColorTexture: baseColorTexture != null ? new MaterialTextureRef(baseColorTexture.Uri) : null,
            metallicRoughnessTexture: mrTexture != null ? new MaterialTextureRef(mrTexture.Uri) : null);

        return new MaterialSource(
            schema: "oxygen.material.v1",
            type: "PBR",
            name: mat.Name,
            pbrMetallicRoughness: pbrData,
            normalTexture: normalTexture != null ? new NormalTextureRef(normalTexture.Uri, normalTexture.Scale) : null,
            occlusionTexture: occlusionTexture != null ? new OcclusionTextureRef(occlusionTexture.Uri, occlusionTexture.Strength) : null,
            alphaMode: MaterialAlphaMode.Opaque, // MVP default
            alphaCutoff: 0.5f,
            doubleSided: mat.DoubleSided);
    }

    private record TextureSlot(string Uri, float Scale, float Strength);

    private static TextureSlot? GetTextureSlot(MaterialChannel? channel, Dictionary<int, string> texturePaths)
    {
        if (channel is not { } c || c.Texture == null)
        {
            return null;
        }

        var texIndex = c.Texture.LogicalIndex;
        if (texturePaths.TryGetValue(texIndex, out var virtualPath))
        {
            var uri = "asset://" + virtualPath;
            return new TextureSlot(uri, 1.0f, 1.0f);
        }

        return null;
    }

    private static (ImportedGeometry metadata, GeometryData data) BuildGeometry(
        SharpGLTF.Schema2.Mesh mesh,
        string sourcePath,
        int meshIndex,
        Dictionary<int, AssetKey> materialKeys)
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

            var matKey = prim.Material != null && materialKeys.TryGetValue(prim.Material.LogicalIndex, out var k)
                ? k
                : new AssetKey(0, 0);

            subMeshes.Add(
                new ImportedSubMesh(
                    Name: string.Create(CultureInfo.InvariantCulture, $"Prim{primIndex:0000}"),
                    MaterialAssetKey: matKey,
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

        var metadata = new ImportedGeometry(
            Schema: "oxygen.geometry.v1",
            Name: string.IsNullOrWhiteSpace(mesh.Name) ? "Mesh" : mesh.Name,
            Source: sourcePath,
            MeshIndex: meshIndex,
            Bounds: new ImportedBounds(boundsMin, boundsMax),
            SubMeshes: subMeshes);

        var data = new GeometryData(
            Vertices: vertices,
            Indices: indices);

        return (metadata, data);
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
