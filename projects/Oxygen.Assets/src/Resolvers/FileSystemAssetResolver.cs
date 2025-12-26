// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Oxygen.Assets.Catalog;
using Oxygen.Assets.Import.Geometry;
using Oxygen.Assets.Import.Materials;
using Oxygen.Assets.Model;

namespace Oxygen.Assets.Resolvers;

/// <summary>
/// Resolves assets from the "Content" mount point (user project folder).
/// </summary>
/// <remarks>
/// <para>
/// This resolver maps URIs to file system paths under <c>{ProjectRoot}/Content/</c> and loads
/// the source representation of the asset (e.g. <c>*.omat.json</c> for materials).
/// </para>
/// <para>
/// This is primarily used by the Editor to inspect and modify asset properties.
/// </para>
/// </remarks>
/// <remarks>
/// Initializes a new instance of the <see cref="FileSystemAssetResolver"/> class.
/// </remarks>
/// <param name="mountPoint">The mount point this resolver handles (e.g. "Content").</param>
/// <param name="sourceRoot">The root directory for source assets.</param>
public sealed class FileSystemAssetResolver(string mountPoint, string sourceRoot) : IAssetResolver
{
    private readonly string mountPoint = mountPoint;

    /// <inheritdoc/>
    public bool CanResolve(string mountPoint)
        => string.Equals(mountPoint, this.mountPoint, StringComparison.OrdinalIgnoreCase);

    /// <inheritdoc/>
    public async Task<Asset?> ResolveAsync(Uri uri)
    {
        var uriMountPoint = AssetUriHelper.GetMountPoint(uri);
        if (!this.CanResolve(uriMountPoint))
        {
            return null;
        }

        // Map URI to file system path
        // asset:///Content/Materials/Wood.omat -> {SourceRoot}/Materials/Wood.omat
        var relativePath = AssetUriHelper.GetRelativePath(uri);
        var assetPath = Path.Combine(sourceRoot, relativePath);

        // Determine asset type from extension
        var extension = Path.GetExtension(assetPath);

        if (string.Equals(extension, ".omat", StringComparison.OrdinalIgnoreCase))
        {
            return await LoadMaterialAsync(assetPath, uri).ConfigureAwait(false);
        }

        if (string.Equals(extension, ".ogeo", StringComparison.OrdinalIgnoreCase))
        {
            return await this.ResolveGeometryAsync(relativePath, uri).ConfigureAwait(false);
        }

        // TODO: Handle other asset types (e.g. .oscene) when their source formats are defined.
        // For now, we return null for unsupported types.
        return null;
    }

    private static GeometryAsset MapGeometry(ImportedGeometry imported, Uri uri)
    {
        var subMeshes = imported.SubMeshes.Select(sm => new SubMesh
        {
            Name = sm.Name,
            MaterialIndex = 0, // Placeholder
        }).ToList();

        return new GeometryAsset
        {
            Uri = uri,
            Source = imported,
            Lods = [
                new MeshLod
                {
                    LodIndex = 0,
                    SubMeshes = subMeshes,
                },
            ],
        };
    }

    private static async Task<MaterialAsset?> LoadMaterialAsync(string assetPath, Uri uri)
    {
        // Material source file is expected to be <AssetName>.omat.json
        // But wait, the URI is usually virtual, e.g. Wood.omat.
        // The source file on disk is Wood.omat.json.
        var sourcePath = assetPath + ".json";

        if (!File.Exists(sourcePath))
        {
            return null;
        }

        try
        {
            var jsonBytes = await File.ReadAllBytesAsync(sourcePath).ConfigureAwait(false);
            var source = MaterialSourceReader.Read(jsonBytes);

            return new MaterialAsset
            {
                Uri = uri,
                Source = source,
            };
        }
        catch (JsonException)
        {
            // TODO: Log error
            return null;
        }
        catch (IOException)
        {
            // TODO: Log error
            return null;
        }
    }

    private async Task<GeometryAsset?> ResolveGeometryAsync(string relativePath, Uri uri)
    {
        // The metadata is in the source folder as .ogeo.json
        var sourcePath = Path.Combine(sourceRoot, relativePath + ".json");

        if (!File.Exists(sourcePath))
        {
            return null;
        }

        try
        {
            var jsonBytes = await File.ReadAllBytesAsync(sourcePath).ConfigureAwait(false);
            var geometry = JsonSerializer.Deserialize<ImportedGeometry>(jsonBytes, new JsonSerializerOptions { PropertyNameCaseInsensitive = true });
            if (geometry != null)
            {
                return MapGeometry(geometry, uri);
            }
        }
        catch (Exception)
        {
            // TODO: Log error
            return null;
        }

        return null;
    }
}
