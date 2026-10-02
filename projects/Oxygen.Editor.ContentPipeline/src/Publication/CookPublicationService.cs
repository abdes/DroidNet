// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using DroidNet.Storage;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>Owns immutable project publication, recovery and current-workspace preview admission.</summary>
public sealed partial class CookPublicationService(IContentCookCoordinator coordinator, IProjectContextService projects,
    IAtomicFileStore files, IProjectManagerService projectManager)
{
    private readonly Lock sync = new();
    private PreviewRegistration? registration;

    /// <summary>Registers the active workspace without starting an engine merely to cook saved content.</summary>
    /// <param name="project">The workspace's project lifetime.</param>
    /// <param name="createPreview">Captures a publication preview owner, or null when no runtime is running.</param>
    /// <returns>A registration disposed when the workspace closes.</returns>
    public IDisposable RegisterPreview(ProjectContext project, Func<Task<ICookPublicationPreview?>> createPreview)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(createPreview);
        lock (this.sync)
        {
            var next = new PreviewRegistration(this, project, createPreview);
            this.registration = next;
            return next;
        }
    }

    internal Task<CookStagingArea> CreateStagingAsync(ContentCookOperation operation, CookPublicationReadLease baseline,
        IEnumerable<string> mounts, IReadOnlySet<string> emptyRoots, CancellationToken cancellationToken)
    {
        coordinator.VerifyWriter(operation);
        return CookStagingArea.CreateAsync(operation, baseline, mounts, files, projectManager, cancellationToken, emptyRoots);
    }

    internal async Task<ContentCookResult> PublishAsync(ContentCookOperation operation, CookStagingArea staging,
        ContentCookResult result, CookProvenance provenance, CookedLibraryReadSet libraries, CancellationToken cancellationToken)
    {
        coordinator.VerifyWriter(operation);
        var preview = await this.CapturePreviewAsync(operation.Project).ConfigureAwait(false);
        try
        {
            var snapshot = result.InputSnapshot ?? throw new InvalidOperationException("Publication requires captured saved inputs.");
            var sealedRoots = staging.SealRoots();
            if (sealedRoots.Any(root => !provenance.Roots.Any(proof => proof.Mount == root.Name && proof.SourceKey == root.SourceKey
                && string.Equals(proof.IndexSha256, root.IndexSha256, StringComparison.OrdinalIgnoreCase))))
            {
                throw new InvalidDataException("Product provenance does not describe the final validated candidate roots.");
            }

            var owned = provenance.Roots.Select(static root => new CookPublicationRoot(CookPublicationRootOwner.Project, root.Mount, root.SourceKey, root.IndexSha256, LibraryPath: null));
            var document = new CookPublicationDocument(CookPublicationDocument.CurrentVersion, operation.Project.ProjectId,
                operation.OperationId, DateTimeOffset.UtcNow, CookPublicationDocument.ConfigurationIdentity(operation.Project),
                libraries.OrderBindings(owned), provenance.Products,
                new(snapshot.BuildFingerprint, snapshot.InputIdentity, snapshot.Inputs, snapshot.Documents, snapshot.CookedDependencies,
                    [.. result.ProducedSourceFiles.Select(static file => new CookPublicationSourceTransition(file.RelativePath, file.BeforeHash, file.AfterHash))]));
            await staging.Transaction.PrepareAsync(operation, document, snapshot.SourceReplacement, result.ProducedSourceFiles,
                projectChange: null, cancellationToken).ConfigureAwait(false);
            staging.RetainForPublication();
            CookRunContext.Report(new(Message: "Publishing cooked content.", State: CookRunState.Publishing));
            using var accepted = await staging.Transaction.PublishAsync(preview, staging.Baseline, () => coordinator.VerifyWriter(operation), cancellationToken).ConfigureAwait(false);
            var published = result with { IsPublished = true, IsMounted = preview?.IsRuntimeAvailable == true };
            if (preview is not null)
            {
                try
                {
                    await preview.CommittedAsync(accepted).ConfigureAwait(false);
                }
                catch (Exception refresh) when (refresh is IOException or InvalidDataException or InvalidOperationException or OperationCanceledException)
                {
                    published = WithWarning(operation.OperationId, published, "Cook.CatalogRefreshFailed",
                        "Cooked content was published. The browser could not finish refreshing it.", refresh);
                }
            }

            return published;
        }
        catch (Exception failure) when (failure is IOException or InvalidDataException or AggregateException or InvalidOperationException)
        {
            return result with
            {
                Status = OperationStatus.Failed,
                Diagnostics =
                [
                    .. result.Diagnostics,
                    new DiagnosticRecord
                    {
                        OperationId = operation.OperationId, Domain = FailureDomain.ContentPipeline, Severity = DiagnosticSeverity.Error,
                        Code = failure is CookOutputBusyException ? "Cook.OutputBusy" : failure is AggregateException ? "Cook.RecoveryRequired" : "Cook.PublicationFailed",
                        Message = failure.Message, TechnicalMessage = failure.ToString(), ExceptionType = failure.GetType().FullName,
                        AffectedPath = Path.Combine(operation.Project.ProjectRoot, ".build", "cook", operation.OperationId.ToString("N")),
                    },
                ],
            };
        }
        finally
        {
            if (preview is not null)
            {
                await preview.DisposeAsync().ConfigureAwait(false);
            }
        }
    }

    /// <summary>Observes configured libraries during owned project startup and returns one mount/catalog snapshot.</summary>
    public async Task<CookPublicationReadLease> AcquireForMountAsync(ProjectContext project, CancellationToken cancellationToken)
    {
        CookPublicationReadLease? accepted = null;
        try
        {
            await coordinator.RunAsync(async (operation, token) =>
            {
                if (!ReferenceEquals(operation.Project, project))
                {
                    throw new OperationCanceledException("The project changed before cooked-content startup.");
                }

                using var baseline = await this.AcquireForOperationAsync(operation, token).ConfigureAwait(false);
                using var libraries = await CookedLibraryReadSet.AcquireAsync(project, token).ConfigureAwait(false);
                var document = BuildMetadataDocument(operation, project, baseline, libraries);
                if ((baseline.Document is { } current
                        && string.Equals(current.MountConfigurationIdentity, document.MountConfigurationIdentity, StringComparison.Ordinal)
                        && current.Roots.SequenceEqual(document.Roots))
                    || (baseline.Document is null && document.Roots.IsEmpty))
                {
                    accepted = baseline.Retain();
                    return true;
                }

                var transaction = await CookPublicationTransaction.ReserveAsync(operation, baseline, [], files, projectManager, token).ConfigureAwait(false);
                await transaction.PrepareAsync(operation, document, sourceReplacement: null, [], projectChange: null, token).ConfigureAwait(false);
                accepted = await transaction.PublishAsync(preview: null, baseline, () => coordinator.VerifyWriter(operation), token).ConfigureAwait(false);
                return true;
            }, cancellationToken).ConfigureAwait(false);
            return accepted ?? throw new InvalidOperationException("Cooked-content startup did not capture a publication.");
        }
        catch
        {
            accepted?.Dispose();
            throw;
        }
    }

    /// <summary>Commits confirmed mount intent and one effective root order without recooking unchanged products.</summary>
    public async Task<CookPublicationReadLease> ChangeMountsAsync(ContentCookOperation operation, ProjectContext next,
        IProjectInfo expected, IProjectInfo desired, CancellationToken cancellationToken)
    {
        coordinator.VerifyWriter(operation);
        using var baseline = await this.AcquireForOperationAsync(operation, cancellationToken).ConfigureAwait(false);
        using var libraries = await CookedLibraryReadSet.AcquireAsync(next, cancellationToken).ConfigureAwait(false);
        var document = BuildMetadataDocument(operation, next, baseline, libraries);
        var transaction = await CookPublicationTransaction.ReserveAsync(operation, baseline, [], files, projectManager, cancellationToken).ConfigureAwait(false);
        await transaction.PrepareAsync(operation, document, sourceReplacement: null, [],
            new(ProjectInfo.ToJson(expected), ProjectInfo.ToJson(desired)), cancellationToken).ConfigureAwait(false);
        var preview = await this.CapturePreviewAsync(operation.Project).ConfigureAwait(false);
        try
        {
            return await transaction.PublishAsync(preview, baseline, () => coordinator.VerifyWriter(operation), cancellationToken).ConfigureAwait(false);
        }
        finally
        {
            if (preview is not null)
            {
                await preview.DisposeAsync().ConfigureAwait(false);
            }
        }
    }

    private static CookPublicationDocument BuildMetadataDocument(ContentCookOperation operation, ProjectContext selected,
        CookPublicationReadLease baseline, CookedLibraryReadSet libraries)
    {
        var names = selected.AuthoringMounts.Select(static mount => mount.Name).ToHashSet(StringComparer.OrdinalIgnoreCase);
        var owned = baseline.Roots.Where(root => root.Owner == CookPublicationRootOwner.Project && names.Contains(root.Name)).ToArray();
        var products = baseline.ProductState.Products.Where(product => product.Outputs.All(output => owned.Any(root => root.Name == output.RootMount))).ToImmutableArray();
        return new(CookPublicationDocument.CurrentVersion, selected.ProjectId, operation.OperationId, DateTimeOffset.UtcNow,
            CookPublicationDocument.ConfigurationIdentity(selected), libraries.OrderBindings(owned), products, baseline.Document?.CookInputs);
    }

    internal async Task<ContentCookResult> PublishObservedBindingsAsync(ContentCookOperation operation, CookPublicationReadLease baseline,
        CookedLibraryReadSet libraries, ContentCookResult result, CancellationToken cancellationToken)
    {
        coordinator.VerifyWriter(operation);
        var document = BuildMetadataDocument(operation, operation.Project, baseline, libraries);
        if ((baseline.Document is { } current && current.MountConfigurationIdentity == document.MountConfigurationIdentity
                && current.Roots.SequenceEqual(document.Roots)) || (baseline.Document is null && document.Roots.IsEmpty))
        {
            return result;
        }

        var transaction = await CookPublicationTransaction.ReserveAsync(operation, baseline, [], files, projectManager, cancellationToken).ConfigureAwait(false);
        await transaction.PrepareAsync(operation, document, sourceReplacement: null, [], projectChange: null, cancellationToken).ConfigureAwait(false);
        var preview = await this.CapturePreviewAsync(operation.Project).ConfigureAwait(false);
        try
        {
            using var accepted = await transaction.PublishAsync(preview, baseline, () => coordinator.VerifyWriter(operation), cancellationToken).ConfigureAwait(false);
            result = result with { IsPublished = true, IsMounted = preview?.IsRuntimeAvailable == true };
            if (preview is not null)
            {
                try
                {
                    await preview.CommittedAsync(accepted).ConfigureAwait(false);
                }
                catch (Exception failure) when (failure is IOException or InvalidDataException or InvalidOperationException or OperationCanceledException)
                {
                    result = WithWarning(operation.OperationId, result, "Cook.CatalogRefreshFailed",
                        "Content bindings were published. The browser could not finish refreshing them.", failure);
                }
            }

            return result;
        }
        finally
        {
            if (preview is not null)
            {
                await preview.DisposeAsync().ConfigureAwait(false);
            }
        }
    }

    private static ContentCookResult WithWarning(Guid operationId, ContentCookResult result, string code, string message, Exception failure)
        => result with
        {
            Status = OperationStatus.SucceededWithWarnings,
            Diagnostics = [.. result.Diagnostics, new DiagnosticRecord
            {
                OperationId = operationId, Domain = FailureDomain.ContentPipeline, Severity = DiagnosticSeverity.Warning,
                Code = code, Message = message, TechnicalMessage = failure.ToString(),
            }],
        };

    private Task<ICookPublicationPreview?> CapturePreviewAsync(ProjectContext project)
    {
        Func<Task<ICookPublicationPreview?>>? create;
        lock (this.sync)
        {
            create = ReferenceEquals(project, projects.ActiveProject) && this.registration is { } current && ReferenceEquals(current.Project, project)
                ? current.CreatePreview : null;
        }

        return create is null ? Task.FromResult<ICookPublicationPreview?>(null) : create();
    }

    private void Unregister(PreviewRegistration owner)
    {
        lock (this.sync)
        {
            if (ReferenceEquals(this.registration, owner))
            {
                this.registration = null;
            }
        }
    }

    private sealed partial class PreviewRegistration(CookPublicationService owner, ProjectContext project, Func<Task<ICookPublicationPreview?>> createPreview) : IDisposable
    {
        public ProjectContext Project { get; } = project;

        public Func<Task<ICookPublicationPreview?>> CreatePreview { get; } = createPreview;

        public void Dispose() => owner.Unregister(this);
    }
}
