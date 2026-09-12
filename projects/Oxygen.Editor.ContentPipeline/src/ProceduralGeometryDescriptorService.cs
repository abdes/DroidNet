// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Materializes engine-owned procedural descriptors in the project's derived input scope.</summary>
/// <param name="catalogProvider">The engine authority for recipes, bounds, materials, and identity.</param>
public sealed class ProceduralGeometryDescriptorService(IBuiltinGeometryCatalogProvider catalogProvider) : IProceduralGeometryDescriptorService
{
    /// <inheritdoc/>
    public async Task<IReadOnlyList<ContentCookInput>> EnsureDescriptorsAsync(
        ContentCookScope scope,
        IReadOnlyList<Uri> geometryUris,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(scope);
        ArgumentNullException.ThrowIfNull(geometryUris);
        cancellationToken.ThrowIfCancellationRequested();
        if (geometryUris.Count == 0)
        {
            return [];
        }

        var catalog = await catalogProvider.GetBuiltinGeometryCatalogAsync(scope.InputRoot, AssetUris.ContentMountPoint, cancellationToken, scope.Artifacts).ConfigureAwait(false);
        var generated = new List<ContentCookInput>();
        foreach (var uri in geometryUris.Distinct())
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (catalog.Find(uri) is not { } definition)
            {
                continue;
            }

            if (generated.Count == 0)
            {
                generated.Add(await WriteContributionAsync(
                    scope,
                    catalog.MountName,
                    catalog.DefaultMaterial,
                    new Uri($"asset://{catalog.DefaultMaterial.VirtualPath}.json"),
                    ContentCookAssetKind.Material,
                    "Materials",
                    ".omat.json",
                    cancellationToken).ConfigureAwait(false));
            }

            generated.Add(await WriteContributionAsync(
                scope,
                catalog.MountName,
                definition.Contribution,
                uri,
                ContentCookAssetKind.Geometry,
                "Geometry",
                ".ogeo.json",
                cancellationToken).ConfigureAwait(false));
        }

        return generated;
    }

    /// <summary>Identifies the authored generated-shape namespace without deciding generator support.</summary>
    /// <param name="uri">The authored identity.</param>
    /// <returns>Whether the identity belongs to the generated shape namespace.</returns>
    internal static bool IsGeneratedBasicShape(Uri uri)
        => string.Equals(uri.Scheme, AssetUris.Scheme, StringComparison.OrdinalIgnoreCase)
            && uri.AbsolutePath.StartsWith("/Engine/Generated/BasicShapes/", StringComparison.OrdinalIgnoreCase);

    private static async Task<ContentCookInput> WriteContributionAsync(
        ContentCookScope scope,
        string mount,
        BuiltinDescriptorContribution contribution,
        Uri authoredUri,
        ContentCookAssetKind kind,
        string folder,
        string extension,
        CancellationToken cancellationToken)
    {
        var relative = Path.Combine(".pipeline", folder, contribution.Name + extension);
        var path = Path.Combine(scope.InputRoot, relative);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var stream = File.Create(path);
        await using (stream.ConfigureAwait(false))
        {
            await JsonSerializer.SerializeAsync(stream, contribution.Descriptor, SceneDescriptorJson.Options, cancellationToken).ConfigureAwait(false);
        }

        return new(authoredUri, kind, mount, relative.Replace('\\', '/'), path, contribution.VirtualPath, ContentCookInputRole.GeneratedDescriptor);
    }
}
