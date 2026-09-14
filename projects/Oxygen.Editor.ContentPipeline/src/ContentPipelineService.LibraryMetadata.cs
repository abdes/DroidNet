// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Storage.Native;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Assets.Catalog.LooseCooked;
using Testably.Abstractions;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Uses the project writer to populate derived library metadata without creating cooking runs.</summary>
public sealed partial class ContentPipelineService : ICookedLibraryMetadataService
{
    /// <inheritdoc />
    public Task<bool> RefreshLibraryMetadataAsync(ProjectContext project, CancellationToken cancellationToken)
        => project.LocalFolderMounts.Count == 0 || this.engineContentPipelineApi is not ICookedDependencyInspector inspector
            ? Task.FromResult(false)
            : this.cookCoordinator.RunAsync(
                (operation, token) => this.RefreshLibraryMetadataCoreAsync(operation, project, inspector, token), cancellationToken);

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Reliability", "CA2025:Do not pass 'IDisposable' instances into unawaited tasks", Justification = "Failed termination transfers the reader into the returned drain task and clears local ownership before finally; the project coordinator retains that drain.")]
    private static async Task<bool> InspectLibraryMetadataAsync(ContentCookOperation operation, string root, ICookedDependencyInspector inspector, CancellationToken cancellationToken)
    {
        var reader = await CookOutputReadLease.AcquireAsync(root, cancellationToken).ConfigureAwait(false);
        try
        {
            using var catalog = new LooseCookedIndexAssetCatalog(new NativeStorageProvider(new RealFileSystem()), new LooseCookedIndexAssetCatalogOptions { CookedRootFolderPath = root });
            var records = await catalog.QueryAsync(new(AssetQueryScope.All), cancellationToken).ConfigureAwait(false);
            await reader.VerifyDescriptorsAsync(records, cancellationToken).ConfigureAwait(false);
            var fingerprint = await CookedDependencyCache.FingerprintAsync(reader, cancellationToken).ConfigureAwait(false);
            if (await CookedDependencyCache.ReadAsync(operation.Project.ProjectRoot, fingerprint, records, cancellationToken).ConfigureAwait(false) is not null)
            {
                return false;
            }

            _ = await CookedDependencyCache.EnsureAsync(operation.Project.ProjectRoot, root, fingerprint, records, inspector, Path.Combine(operation.Project.ProjectRoot, ".build", "cook", operation.OperationId.ToString("N")), cancellationToken).ConfigureAwait(false);
            _ = reader.GetFiles();
            return true;
        }
        catch (ContentPipelineTerminationException failure)
        {
            var retained = reader;
            reader = null;
            var drain = ReleaseLibraryMetadataReaderAsync(failure.DrainCompletion, retained);
            throw new ContentPipelineTerminationException(failure.InnerException ?? failure, drain);
        }
        finally
        {
            if (reader is not null)
            {
                await reader.DisposeAsync().ConfigureAwait(false);
            }
        }
    }

    private static async Task ReleaseLibraryMetadataReaderAsync(Task drain, CookOutputReadLease reader)
    {
        try
        {
            await drain.ConfigureAwait(false);
        }
        finally
        {
            await reader.DisposeAsync().ConfigureAwait(false);
        }
    }

    private async Task<bool> RefreshLibraryMetadataCoreAsync(ContentCookOperation operation, ProjectContext project, ICookedDependencyInspector inspector, CancellationToken cancellationToken)
    {
        if (!ReferenceEquals(operation.Project, project))
        {
            return false;
        }

        var changed = false;
        foreach (var library in project.LocalFolderMounts)
        {
            if (File.Exists(Path.Combine(library.AbsolutePath, "container.index.bin")))
            {
                changed |= await InspectLibraryMetadataAsync(operation, library.AbsolutePath, inspector, cancellationToken).ConfigureAwait(false);
            }
        }

        if (inspector is ICookedAssetKeyProvider keys)
        {
            changed |= await this.RefreshProjectIdentitiesAsync(operation, keys, cancellationToken).ConfigureAwait(false);
        }

        this.cookCoordinator.VerifyCurrent(operation);
        return changed;
    }

    private async Task<bool> RefreshProjectIdentitiesAsync(ContentCookOperation operation, ICookedAssetKeyProvider provider, CancellationToken cancellationToken)
    {
        // Identity lookup reads only its private path request, so library readers can close before it starts.
        using (var libraries = await Publication.CookedLibraryReadSet.AcquireAsync(operation.Project, cancellationToken).ConfigureAwait(false))
        {
            if (!libraries.HasUnresolvedAssetKeys)
            {
                return false;
            }
        }

        var (previous, _) = await this.provenanceStore.ReadAsync(operation.Project, cancellationToken).ConfigureAwait(false);
        if (!await this.publication.HasCommittedMetadataAsync(operation.Project, cancellationToken).ConfigureAwait(false))
        {
            previous = new(1, operation.Project.ProjectId, [], []);
        }

        var imports = await Import.ImportedSourceIndex.ReadAsync(operation.Project, cookDocuments, previous, cancellationToken).ConfigureAwait(false);
        var candidates = await ProjectAssetKeyIndex.ReadAsync(operation.Project, imports.KnownOutputs, cancellationToken).ConfigureAwait(false);
        return await candidates.EnsureAsync(provider, Path.Combine(operation.Project.ProjectRoot, ".build", "cook", operation.OperationId.ToString("N")), cancellationToken).ConfigureAwait(false);
    }
}
