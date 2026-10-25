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
        using var libraries = await CookedLibraryReadSet.AcquireAsync(project, this.engineContent, token, uri => ImportedOutputs.ResolveOutput(project, uri, ContentCookInputRole.Dependency), [.. publicationRead.ProductState.ProducedAssets]).ConfigureAwait(false);
        var read = new GeometryRead(project, geometryUri, inspector, Path.Combine(project.ProjectRoot.FullPath, ".build", "inspection"));
        try
        {
            return libraries.FindPreferredAsset(geometryUri) is { } library
                ? await this.ReadLibraryGeometryAsync(read, libraries, library, token).ConfigureAwait(false)
                : await this.ReadProjectGeometryAsync(read, publicationRead, token).ConfigureAwait(false);
        }
        catch (ContentPipelineTerminationException failure)
        {
            await failure.DrainCompletion.ConfigureAwait(false);
            throw;
        }
    }

    private static bool HasCurrentOutput(AssetCookStatus status)
        => status.Freshness == AssetCookFreshness.Current && status.HasAvailableOutput && !status.HasUnsavedChanges;

    private static GeometrySurfaceMetadata? VerifyInventoryKey(GeometrySurfaceMetadata? metadata, string expectedIndexKey)
    {
        if (metadata is not null && !string.Equals(metadata.NativeGeometryKey.ToString("D"), expectedIndexKey, StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidDataException("Native geometry metadata does not match the selected source's asset identity.");
        }

        return metadata;
    }

    private async Task<GeometrySurfaceMetadata?> ReadLibraryGeometryAsync(GeometryRead read, CookedLibraryReadSet libraries, CookedLibraryReadSet.PreferredLibraryAsset library, CancellationToken token)
    {
        await libraries.ValidateNativeAsync(this.engineContentPipelineApi, token).ConfigureAwait(false);
        var native = library.Asset.Cooked;
        if (native is null || native.AssetType != 2 || native.VirtualPath is not { } virtualPath)
        {
            return null;
        }

        var report = await read.Inspector.InspectGeometryAsync(read.OperationRoot, library.CookedRoot, virtualPath.VirtualPath, token).ConfigureAwait(false);
        token.ThrowIfCancellationRequested();
        return VerifyInventoryKey(report.SingleOrDefault(read.GeometryUri), native.AssetKey.ToString());
    }

    private async Task<GeometrySurfaceMetadata?> ReadProjectGeometryAsync(GeometryRead read, CookPublicationReadLease publicationRead, CancellationToken token)
    {
        var status = (await this.statusReader.ReadAsync(read.Project, publicationRead, [read.GeometryUri], token).ConfigureAwait(false)).Single();
        if (!HasCurrentOutput(status))
        {
            return null;
        }

        var matches = status.Outputs.Where(output => output.Kind == ContentCookAssetKind.Geometry
            && (output.CookedAssetUri == read.GeometryUri || output.SourceAssetUri == read.GeometryUri)).Take(2).ToArray();
        var prior = publicationRead.ProductState;
        var owner = matches.Length == 1 ? prior.Products.SelectMany(static product => product.Outputs).SingleOrDefault(output => output.Asset == matches[0]) : null;
        var expectedRoot = owner is null ? null : prior.Roots.FirstOrDefault(root => string.Equals(root.Mount, owner.RootMount, StringComparison.Ordinal));
        if (owner is null || expectedRoot is null)
        {
            return null;
        }

        var selected = matches[0];
        var cookedRoot = publicationRead.FindProjectRoot(owner.RootMount)?.FullPath
            ?? throw new InvalidDataException("Geometry output has no selected generation.");
        var outputs = await CookOutputReadLease.AcquireAsync(cookedRoot, token).ConfigureAwait(false);
        await using var lifetime = outputs.ConfigureAwait(false);
        var inventory = await outputs.ReadInventoryAsync(this.engineContentPipelineApi, token).ConfigureAwait(false);
        var indexed = expectedRoot.Matches(inventory) && inventory.IsValid
            ? inventory.Assets.SingleOrDefault(asset => string.Equals(asset.VirtualPath, selected.VirtualPath, StringComparison.Ordinal))
            : null;
        if (indexed is null)
        {
            return null;
        }

        CookedGeometryReport geometryReport;
        try
        {
            geometryReport = await read.Inspector.InspectGeometryAsync(read.OperationRoot, cookedRoot, selected.VirtualPath, token).ConfigureAwait(false);
        }
        catch (ContentPipelineTerminationException failure)
        {
            // The native reader drains before the read lease releases the root's files.
            await failure.DrainCompletion.ConfigureAwait(false);
            throw;
        }

        _ = outputs.GetFiles();
        var latest = (await this.statusReader.ReadAsync(read.Project, publicationRead, [read.GeometryUri], token).ConfigureAwait(false)).Single();
        token.ThrowIfCancellationRequested();
        return HasCurrentOutput(latest) && latest.Outputs.Contains(selected)
            ? VerifyInventoryKey(geometryReport.SingleOrDefault(read.GeometryUri), indexed.Key.ToString())
            : null;
    }

    /// <summary>One surface read: the geometry and the native reader.</summary>
    private sealed record GeometryRead(ProjectContext Project, AssetPath GeometryUri, ICookedGeometryInspector Inspector, string OperationRoot);
}
