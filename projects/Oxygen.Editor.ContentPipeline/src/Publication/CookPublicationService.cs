// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using DroidNet.Storage;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>Connects journaled publication to the current workspace's preview ownership.</summary>
/// <param name="coordinator">The shared cook writer and project lifetime.</param>
/// <param name="projects">The active project registry.</param>
/// <param name="files">The atomic metadata store.</param>
public sealed partial class CookPublicationService(IContentCookCoordinator coordinator, IProjectContextService projects, IAtomicFileStore files)
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

    /// <summary>Commits validated staging and returns the resulting published identities.</summary>
    /// <param name="operation">The owning cook.</param>
    /// <param name="staging">The private validated roots.</param>
    /// <param name="result">The native cook result.</param>
    /// <param name="provenance">The complete updated product evidence.</param>
    /// <param name="cancellationToken">Cancels before replacement.</param>
    /// <returns>The publication outcome with scoped diagnostics.</returns>
    internal async Task<ContentCookResult> PublishAsync(ContentCookOperation operation, CookStagingArea staging, ContentCookResult result, CookProvenance provenance, CancellationToken cancellationToken)
    {
        coordinator.VerifyWriter(operation);
        var preview = await this.CapturePreviewAsync(operation.Project).ConfigureAwait(false);
        try
        {
            var snapshot = result.InputSnapshot ?? throw new InvalidOperationException("Publication requires captured saved inputs.");
            var receipt = new CookPublicationReceipt(
                1,
                operation.Project.ProjectId,
                operation.OperationId,
                DateTimeOffset.UtcNow,
                snapshot.BuildFingerprint,
                snapshot.InputIdentity,
                snapshot.Inputs,
                snapshot.Documents,
                provenance.Roots,
                preview?.IsRuntimeAvailable == true);
            var metadata = new Dictionary<string, byte[]>(StringComparer.Ordinal)
            {
                [CookPublicationTransaction.PublicationMetadata] = JsonSerializer.SerializeToUtf8Bytes(receipt),
                [CookPublicationTransaction.ProvenanceMetadata] = CookProvenanceStore.Serialize(operation.Project, provenance),
            };
            var transaction = await CookPublicationTransaction.PrepareAsync(operation, staging, metadata, files, cancellationToken).ConfigureAwait(false);
            CookRunContext.Report(new(Message: "Publishing cooked content.", State: CookRunState.Publishing));
            await transaction.PublishAsync(preview, () => coordinator.VerifyWriter(operation), cancellationToken).ConfigureAwait(false);
            if (transaction.CleanupFailure is { } cleanup)
            {
                result = WithCleanupWarning(operation.OperationId, result, cleanup);
            }

            var roots = staging.Roots.ToDictionary(static root => Path.GetFullPath(root.StagingPath), static root => root.PublishedPath, StringComparer.OrdinalIgnoreCase);
            string PublishedPaths(string paths) => string.Join(Path.PathSeparator, paths.Split(Path.PathSeparator, StringSplitOptions.RemoveEmptyEntries)
                .Select(path => roots.GetValueOrDefault(Path.GetFullPath(path), path)));
            return result with
            {
                IsPublished = true, IsMounted = preview?.IsRuntimeAvailable == true,
                Inspection = result.Inspection is { } inspection ? inspection with { CookedRoot = PublishedPaths(inspection.CookedRoot) } : null,
                Validation = result.Validation is { } validation ? validation with { CookedRoot = PublishedPaths(validation.CookedRoot) } : null,
            };
        }
        catch (Exception failure) when (failure is IOException or InvalidDataException or AggregateException or InvalidOperationException)
        {
            var diagnostic = new DiagnosticRecord
            {
                OperationId = operation.OperationId, Domain = FailureDomain.ContentPipeline, Severity = DiagnosticSeverity.Error,
                Code = failure is CookOutputBusyException ? "Cook.OutputBusy" : failure is AggregateException ? "Cook.RecoveryRequired" : "Cook.PublicationFailed",
                Message = failure.Message, TechnicalMessage = failure.ToString(), ExceptionType = failure.GetType().FullName,
                AffectedPath = Path.Combine(operation.Project.ProjectRoot, ".build", "cook", operation.OperationId.ToString("N")),
            };
            return result with { Status = OperationStatus.Failed, Diagnostics = [.. result.Diagnostics, diagnostic] };
        }
        finally
        {
            if (preview is not null)
            {
                await preview.DisposeAsync().ConfigureAwait(false);
            }
        }
    }

    private static ContentCookResult WithCleanupWarning(Guid operationId, ContentCookResult result, Exception cleanup)
        => result with
        {
            Status = OperationStatus.SucceededWithWarnings,
            Diagnostics =
            [
                .. result.Diagnostics,
                new DiagnosticRecord
                {
                    OperationId = operationId, Domain = FailureDomain.ContentPipeline, Severity = DiagnosticSeverity.Warning,
                    Code = "Cook.CleanupDeferred", Message = "Cooked content was published. Some temporary files could not yet be removed.",
                    TechnicalMessage = cleanup.ToString(),
                },
            ],
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
