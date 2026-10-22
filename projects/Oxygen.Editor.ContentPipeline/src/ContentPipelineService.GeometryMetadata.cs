// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Reads authoritative surface inventories for the source selected by current project policy.</summary>
public sealed partial class ContentPipelineService : IGeometrySurfaceProvider
{
    /// <inheritdoc />
    public async Task<GeometrySurfaceMetadata?> ReadAsync(ProjectContext project, AssetPath geometryUri, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(geometryUri);
        using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, this.session.Lifetime);
        var token = cancellation.Token;
        cancellationToken.ThrowIfCancellationRequested();
        if (!ReferenceEquals(this.session.Context, project))
        {
            return null;
        }

        token.ThrowIfCancellationRequested();

        if (this.engineContentPipelineApi is not ICookedGeometryInspector inspector)
        {
            return null;
        }

        using var publicationRead = await this.publication.AcquireReadAsync(project, token).ConfigureAwait(false);
        var prior = publicationRead.ProductState;

        using var libraries = await CookedLibraryReadSet.AcquireAsync(project, this.engineContent, token, uri => ImportedOutputs.ResolveOutput(project, uri, ContentCookInputRole.Dependency), [.. prior.ProducedAssets]).ConfigureAwait(false);
        var operationRoot = Path.Combine(project.ProjectRoot.FullPath, ".build", "inspection");
        try
        {
            if (libraries.FindPreferredAsset(geometryUri) is { } library)
            {
                await libraries.ValidateNativeAsync(this.engineContentPipelineApi, token).ConfigureAwait(false);
                var native = library.Asset.Cooked;
                if (native is null || native.AssetType != 2 || native.VirtualPath is not { } virtualPath)
                {
                    return null;
                }

                var report = await inspector.InspectGeometryAsync(operationRoot, library.CookedRoot, virtualPath.VirtualPath, token).ConfigureAwait(false);
                token.ThrowIfCancellationRequested();
                return VerifyInventoryKey(report.SingleOrDefault(geometryUri), native.AssetKey.ToString());
            }

            var status = (await this.statusReader.ReadAsync(project, publicationRead, [geometryUri], token).ConfigureAwait(false)).Single();
            if (!HasCurrentGeometryOutput(status))
            {
                return null;
            }

            var matches = status.Outputs.Where(output => output.Kind == ContentCookAssetKind.Geometry
                && (output.CookedAssetUri == geometryUri || output.SourceAssetUri == geometryUri)).Take(2).ToArray();
            if (matches.Length != 1)
            {
                return null;
            }

            var selected = matches[0];
            var owner = prior.Products.SelectMany(static product => product.Outputs).SingleOrDefault(output => output.Asset == selected);
            var expectedRoot = owner is null ? null : prior.Roots.FirstOrDefault(root => string.Equals(root.Mount, owner.RootMount, StringComparison.Ordinal));
            if (owner is null || expectedRoot is null) { return null; }
            var cookedRoot = publicationRead.FindProjectRoot(owner.RootMount)?.FullPath
                ?? throw new InvalidDataException("Geometry output has no selected generation.");
            await using var files = await CookOutputReadLease.AcquireAsync(cookedRoot, token).ConfigureAwait(false);
            var inventory = await files.ReadInventoryAsync(this.engineContentPipelineApi, token).ConfigureAwait(false);
            if (!expectedRoot.Matches(inventory) || !inventory.IsValid) { return null; }
            var indexed = inventory.Assets.SingleOrDefault(asset => string.Equals(asset.VirtualPath, selected.VirtualPath, StringComparison.Ordinal));
            if (indexed is null) { return null; }
            var expectedKey = indexed.Key.ToString();
            CookedGeometryReport geometryReport;
            try
            {
                geometryReport = await inspector.InspectGeometryAsync(operationRoot, cookedRoot, selected.VirtualPath, token).ConfigureAwait(false);
            }
            catch (ContentPipelineTerminationException failure)
            {
                await failure.DrainCompletion.ConfigureAwait(false);
                throw;
            }

            _ = files.GetFiles();
            var latest = (await this.statusReader.ReadAsync(project, publicationRead, [geometryUri], token).ConfigureAwait(false)).Single();
            token.ThrowIfCancellationRequested();
            return HasCurrentGeometryOutput(latest) && latest.Outputs.Contains(selected)
                ? VerifyInventoryKey(geometryReport.SingleOrDefault(geometryUri), expectedKey) : null;
        }
        catch (ContentPipelineTerminationException failure)
        {
            await failure.DrainCompletion.ConfigureAwait(false);
            throw;
        }
    }

    private static bool HasCurrentGeometryOutput(AssetCookStatus status)
        => status.Freshness == AssetCookFreshness.Current && status.HasAvailableOutput && !status.HasUnsavedChanges;

    private static GeometrySurfaceMetadata? VerifyInventoryKey(GeometrySurfaceMetadata? metadata, string expectedIndexKey)
    {
        if (metadata is not null && !string.Equals(metadata.NativeGeometryKey.ToString("D"), expectedIndexKey, StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidDataException("Native geometry metadata does not match the selected source's asset identity.");
        }

        return metadata;
    }
}
