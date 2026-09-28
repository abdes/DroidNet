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

/// <summary>Reads authoritative slot inventories for the source selected by current project policy.</summary>
public sealed partial class ContentPipelineService : IGeometryMaterialSlotProvider
{
    /// <inheritdoc />
    public async Task<GeometryMaterialSlotMetadata?> ReadAsync(ProjectContext project, Uri geometryUri, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(geometryUri);
        if (!geometryUri.IsAbsoluteUri || !string.Equals(geometryUri.Scheme, AssetUris.Scheme, StringComparison.OrdinalIgnoreCase)
            || geometryUri.Query.Length != 0 || geometryUri.Fragment.Length != 0)
        {
            throw new ArgumentException("Geometry metadata requires an asset URI without query or fragment.", nameof(geometryUri));
        }

        using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        using var projectChanges = this.projectContextService.ProjectChanged.Subscribe(current =>
        {
            if (!ReferenceEquals(current, project))
            {
                cancellation.Cancel();
            }
        });
        var token = cancellation.Token;
        cancellationToken.ThrowIfCancellationRequested();
        if (!ReferenceEquals(this.projectContextService.ActiveProject, project))
        {
            return null;
        }

        token.ThrowIfCancellationRequested();

        if (ProceduralGeometryDescriptorService.IsGeneratedBasicShape(geometryUri))
        {
            if (this.engineContentPipelineApi is not IBuiltinGeometryCatalogProvider builtins)
            {
                return null;
            }

            var catalog = await builtins.GetBuiltinGeometryCatalogAsync(project.ProjectRoot, AssetUris.ContentMountPoint, token).ConfigureAwait(false);
            token.ThrowIfCancellationRequested();
            var metadata = catalog.Find(geometryUri)?.MaterialSlots;
            return metadata is null ? null : metadata with { GeometryUri = geometryUri };
        }

        if (this.engineContentPipelineApi is not ICookedGeometryInspector inspector)
        {
            return null;
        }

        using var publicationRead = await CookOutputLease.AcquireInspectionAsync(project.ProjectRoot, token).ConfigureAwait(false);
        var (prior, _) = await this.provenanceStore.ReadAsync(project, token).ConfigureAwait(false);
        if (!await this.publication.HasCommittedMetadataUnderLeaseAsync(project, token).ConfigureAwait(false))
        {
            prior = new(CookProvenance.CurrentVersion, project.ProjectId, [], []);
        }

        var imports = await ImportedSourceIndex.ReadAsync(project, cookDocuments, prior, token).ConfigureAwait(false);
        using var libraries = await CookedLibraryReadSet.AcquireAsync(project, token, uri => imports.ResolveOutput(project, uri, ContentCookInputRole.Dependency), imports.KnownOutputs).ConfigureAwait(false);
        var operationRoot = Path.Combine(project.ProjectRoot, ".build", "inspection");
        try
        {
            if (libraries.FindPreferredAsset(geometryUri) is { } library)
            {
                var native = library.Asset.Cooked;
                if (native is null || native.AssetType != 2 || native.VirtualPath is not { Length: > 0 } virtualPath)
                {
                    return null;
                }

                var report = await inspector.InspectGeometryAsync(operationRoot, library.CookedRoot, virtualPath, token).ConfigureAwait(false);
                token.ThrowIfCancellationRequested();
                return VerifyInventoryKey(report.SingleOrDefault(geometryUri), native.AssetKey.ToString());
            }

            var reader = new AssetCookStatusReader(cookDocuments, this.publication, this.nativeCompatibility,
                provenanceFiles ?? new DroidNet.Storage.Native.NativeAtomicFileStore(new Testably.Abstractions.RealFileSystem()));
            var status = (await reader.ReadUnderInspectionAsync(project, [geometryUri], token).ConfigureAwait(false)).Single();
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
            var indexed = owner is null ? null : prior.Roots.FirstOrDefault(root => string.Equals(root.Mount, owner.RootMount, StringComparison.Ordinal))?.Assets
                .SingleOrDefault(asset => string.Equals(asset.Entry.VirtualPath, selected.VirtualPath, StringComparison.Ordinal));
            if (owner is null || indexed?.Entry.AssetKey is not { Length: > 0 } expectedKey)
            {
                return null;
            }

            var cookedRoot = Path.GetDirectoryName(CookIncrementalPlanner.ResolveOutputPath(project.ProjectRoot, owner.RootMount, "container.index.bin"))!;
            await using var files = await CookOutputReadLease.AcquireAsync(cookedRoot, token).ConfigureAwait(false);
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
            var latest = (await reader.ReadUnderInspectionAsync(project, [geometryUri], token).ConfigureAwait(false)).Single();
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
        => status.Freshness == AssetCookFreshness.Current && status.HasVerifiedOutput && !status.HasUnsavedChanges;

    private static GeometryMaterialSlotMetadata? VerifyInventoryKey(GeometryMaterialSlotMetadata? metadata, string expectedIndexKey)
    {
        if (metadata is not null && !string.Equals(CookedDependencyReport.IndexKey(metadata.NativeGeometryKey.ToString("D")), expectedIndexKey, StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidDataException("Native geometry metadata does not match the selected source's asset identity.");
        }

        return metadata;
    }
}
