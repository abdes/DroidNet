// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Oxygen.Assets.Import.Geometry;
using Oxygen.Assets.Import.Gltf;
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
/// <param name="authority">The authority (mount point) this resolver handles (e.g. "Content").</param>
/// <param name="sourceRoot">The root directory for source assets.</param>
/// <param name="importedRoot">The root directory for imported artifacts.</param>
public sealed class FileSystemAssetResolver(string authority, string sourceRoot, string importedRoot) : IAssetResolver
{
    private readonly string authority = authority;

    /// <inheritdoc/>
    public bool CanResolve(string authority)
        => string.Equals(authority, this.authority, StringComparison.OrdinalIgnoreCase);

    /// <inheritdoc/>
    public async Task<Asset?> ResolveAsync(Uri uri)
    {
        if (!this.CanResolve(uri.Authority))
        {
            return null;
        }

        // Map URI to file system path
        // asset://Content/Materials/Wood.omat -> {SourceRoot}/Materials/Wood.omat
        var relativePath = uri.AbsolutePath.TrimStart('/');
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
        // The artifact resides in the .imported folder.
        var importedPath = Path.Combine(importedRoot, relativePath);

        if (!File.Exists(importedPath))
        {
            return null;
        }

        try
        {
            var stream = File.OpenRead(importedPath);
            await using (stream.ConfigureAwait(false))
            {
                var geometry = await ImportedGeometrySerializer.ReadAsync(stream).ConfigureAwait(false);
                if (geometry != null)
                {
                    return MapGeometry(geometry, uri);
                }
            }
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

        return null;
    }
}
