// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Serializes the cook writers of a project session and drains them when the session closes, before releasing ownership.</summary>
/// <remarks>
///     The coordinator cooks the session's project. A change of the project's content mounts ends the requests made for
///     the earlier mounts, as closing the session ends every request; disposing the coordinator refuses new ones.
/// </remarks>
public sealed partial class ContentCookCoordinator : IContentCookCoordinator, ICookRunService, IDisposable
{
    private readonly Lock stateLock = new();
    private readonly IProjectSession session;
    private readonly ILogger<ContentCookCoordinator> logger;
    private CancellationTokenSource lifetimeCancellation;
    private ProjectContext? project;
    private ContentCookOperation? activeOperation;
    private long lifetime;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="ContentCookCoordinator"/> class.</summary>
    /// <param name="session">The project session whose cooks the coordinator serializes.</param>
    /// <param name="logger">The coordinator logger.</param>
    public ContentCookCoordinator(IProjectSession session, ILogger<ContentCookCoordinator> logger)
    {
        ArgumentNullException.ThrowIfNull(session);
        this.session = session;
        this.project = session.Context;
        this.lifetimeCancellation = CancellationTokenSource.CreateLinkedTokenSource(session.Lifetime);
        this.logger = logger;
        session.ContextChanged += this.OnContextChanged;
    }

    /// <inheritdoc />
    public Task<T> RunAsync<T>(Func<ContentCookOperation, CancellationToken, Task<T>> work, CancellationToken cancellationToken)
        => this.RunCoreAsync(work, request: null, cancellationToken);

    /// <inheritdoc />
    public Task RunProjectChangeAsync(Func<ContentCookOperation, CancellationToken, Task> change, CancellationToken cancellationToken)
        => this.RunCoreAsync(
            async (operation, token) =>
            {
                await change(operation, token).ConfigureAwait(false);
                return true;
            },
            request: null,
            cancellationToken,
            verifyCurrentAfterWork: false);

    /// <inheritdoc />
    public Task<T> RunCookAsync<T>(CookRunRequest request, Func<ContentCookOperation, CancellationToken, Task<T>> work, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(request);
        return request.CoalescePending
            ? this.RunSharedAsync(request, work, cancellationToken)
            : this.RunCoreAsync(work, request, cancellationToken);
    }

    /// <inheritdoc />
    public void VerifyCurrent(ContentCookOperation operation)
    {
        ArgumentNullException.ThrowIfNull(operation);
        lock (this.stateLock)
        {
            if (this.disposed || operation.ProjectLifetime != this.lifetime
                || this.lifetimeCancellation.IsCancellationRequested
                || !ReferenceEquals(operation.Project, this.project))
            {
                throw new OperationCanceledException("The cook request's project was closed or replaced.", new CancellationToken(canceled: true));
            }
        }
    }

    /// <inheritdoc />
    public void VerifyWriter(ContentCookOperation operation)
    {
        lock (this.stateLock)
        {
            this.VerifyCurrent(operation);
            if (!ReferenceEquals(operation, this.activeOperation))
            {
                throw new InvalidOperationException("The cook operation no longer owns the writer.");
            }
        }
    }

    /// <inheritdoc />
    public void Dispose()
    {
        CancellationTokenSource cancellation;
        lock (this.stateLock)
        {
            if (this.disposed)
            {
                return;
            }

            this.disposed = true;
            this.project = null;
            this.lifetime++;
            cancellation = this.lifetimeCancellation;
            this.DispatchNextWriter();
        }

        this.session.ContextChanged -= this.OnContextChanged;
        _ = this.CancelLifetimeAsync(cancellation);
        this.ReleaseRetryInputsIfClosed();
    }

    private static bool IsInOperationFolder(string path, ContentCookOperation operation)
    {
        var folder = operation.Project.ProjectRoot.Combine(Path.Combine(".build", "cook", operation.OperationId.ToString("N")));
        var file = FileSystemPath.From(path);
        return file != folder && file.IsSameOrInside(folder);
    }

    // The requests made for the earlier mounts end; the next ones cook the project as its mounts now are.
    private void OnContextChanged(object? sender, EventArgs args)
    {
        CancellationTokenSource previous;
        lock (this.stateLock)
        {
            if (this.disposed)
            {
                return;
            }

            previous = this.lifetimeCancellation;
            this.lifetimeCancellation = CancellationTokenSource.CreateLinkedTokenSource(this.session.Lifetime);
            this.project = this.session.Context;
            this.lifetime++;
            this.DispatchNextWriter();
        }

        _ = this.CancelLifetimeAsync(previous);
        this.ReleaseRetryInputsIfClosed();
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Reliability", "CA2025:Ensure tasks using 'IDisposable' instances complete before the instances are disposed", Justification = "ReleaseAfterDrain takes ownership of the request cancellation and the operation lease and disposes them when the drain ends; this method no longer uses or disposes them.")]
    private async Task<T> RunCoreAsync<T>(Func<ContentCookOperation, CancellationToken, Task<T>> work, CookRunRequest? request, CancellationToken cancellationToken, SharedCook? shared = null, bool verifyCurrentAfterWork = true)
    {
        ArgumentNullException.ThrowIfNull(work);
        cancellationToken.ThrowIfCancellationRequested();
        var (operation, requestCancellation) = this.CreateRequest(shared?.Request ?? request, shared, cancellationToken);
        IProgress<CookRunProgress>? progress = null;
        var acquired = false;
        var retained = false;
        FileStream? operationOwnership = null;
        try
        {
            progress = request is null ? null : this.AddRun(operation, request, requestCancellation, shared);
            using var reporting = CookRunContext.Enter(progress);
            await this.AcquireWriterAsync(operation, request, shared, requestCancellation.Token).ConfigureAwait(false);
            acquired = true;
            await this.BeginWorkAsync(operation, progress, requestCancellation.Token).ConfigureAwait(false);
            operationOwnership = CookOutputLease.AcquireOperation(operation.Project.ProjectRoot, operation.OperationId);
            progress?.Report(new(Message: "Checking saved inputs and dependencies.", State: CookRunState.Preparing));
            var result = await work.Invoke(operation, requestCancellation.Token).ConfigureAwait(false);
            await this.CompleteWorkAsync(operation, result, verifyCurrentAfterWork, requestCancellation.Token).ConfigureAwait(false);
            return result;
        }
        catch (ContentPipelineTerminationException ex) when (acquired)
        {
            retained = true;
            var drain = this.ReleaseAfterDrain(operation, requestCancellation, operationOwnership, ex, progress);
            operationOwnership = null;
            throw new ContentPipelineTerminationException(ex.InnerException ?? ex, drain);
        }
        catch (Exception ex)
        {
            this.FailRun(operation.OperationId, ex);
            if (this.ToPreempted(operation.OperationId, ex) is { } stopped)
            {
                throw stopped;
            }

            throw;
        }
        finally
        {
            if (!retained)
            {
                this.EndRequest(operation, operationOwnership, acquired, requestCancellation);
            }
        }
    }

    private async Task BeginWorkAsync(ContentCookOperation operation, IProgress<CookRunProgress>? progress, CancellationToken cancellationToken)
    {
        if (progress is not null)
        {
            this.LogRunStarted(operation.OperationId);

            // Cached I/O can complete synchronously; a cook must not run its whole closure on the UI caller.
            await Task.CompletedTask.ConfigureAwait(ConfigureAwaitOptions.ForceYielding);
        }

        cancellationToken.ThrowIfCancellationRequested();
        this.VerifyCurrent(operation);
    }

    private CookPreemptedException? ToPreempted(Guid operationId, Exception exception)
        => exception is OperationCanceledException && this.IsPreempted(operationId)
            ? new CookPreemptedException("The cook on change stopped so that other work on the project could run first.", exception)
            : null;

    private void EndRequest(ContentCookOperation operation, FileStream? ownership, bool acquired, CancellationTokenSource requestCancellation)
    {
        try
        {
            this.RetainRetryInputOrRelease(operation, ownership);
        }
        finally
        {
            this.CompleteRequest(acquired, requestCancellation);
        }
    }

    private Task CompleteWorkAsync<T>(ContentCookOperation operation, T result, bool verifyCurrent, CancellationToken cancellationToken)
    {
        if (!verifyCurrent)
        {
            return Task.CompletedTask;
        }

        if (result is not ContentCookResult { IsPublished: true })
        {
            cancellationToken.ThrowIfCancellationRequested();
        }

        this.VerifyCurrent(operation);
        return this.CompleteRunAsync(operation, result, cancellationToken);
    }

    private Task ReleaseAfterDrain(ContentCookOperation operation, CancellationTokenSource cancellation, FileStream? ownership, ContentPipelineTerminationException failure, IProgress<CookRunProgress>? progress)
    {
        progress?.Report(new(Message: "Unable to stop owned work yet. Waiting for it to drain.", State: CookRunState.Cancelling));
        return failure.DrainCompletion.ContinueWith(
            completed =>
            {
                _ = completed.Exception;
                try
                {
                    this.FailRun(operation.OperationId, failure);
                }
                finally
                {
                    this.EndRequest(operation, ownership, acquired: true, cancellation);
                }
            },
            CancellationToken.None,
            TaskContinuationOptions.ExecuteSynchronously,
            TaskScheduler.Default);
    }

    private (ContentCookOperation Operation, CancellationTokenSource Cancellation) CreateRequest(CookRunRequest? request, SharedCook? shared, CancellationToken cancellationToken)
    {
        lock (this.stateLock)
        {
            if (shared is not null && shared.Key.Lifetime != this.lifetime)
            {
                throw new OperationCanceledException("The shared cook's project was closed or replaced.", new CancellationToken(canceled: true));
            }

            ObjectDisposedException.ThrowIf(this.disposed, this);
            var context = this.project ?? throw new InvalidOperationException("Cooking requires an active project.");
            var requestCancellation = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, this.lifetimeCancellation.Token);
            var operation = new ContentCookOperation(Guid.NewGuid(), context, this.lifetime)
            {
                UpgradesPublication = request is { TargetKind: CookTargetKind.Project, IsAutomatic: false, IsDemand: false },
            };
            shared?.OperationId = operation.OperationId;

            return (operation, requestCancellation);
        }
    }

    private void CompleteRequest(bool acquired, CancellationTokenSource requestCancellation)
    {
        requestCancellation.Dispose();
        if (acquired)
        {
            this.ReleaseWriter();
        }
    }

    private void ReleaseWriter()
    {
        lock (this.stateLock)
        {
            this.activeOperation = null;
            this.DispatchNextWriter();
        }

        this.ReleaseRetryInputsIfClosed();
    }

    private void RetainRetryInputOrRelease(ContentCookOperation operation, FileStream? ownership)
    {
        try
        {
            lock (this.stateLock)
            {
                if (!this.disposed && operation.ProjectLifetime == this.lifetime && ownership is not null
                    && this.runs.TryGetValue(operation.OperationId, out var run)
                    && run.Snapshot.State is CookRunState.Failed or CookRunState.Cancelled
                    && run.Snapshot.Request.Import?.ReplacementCandidatePath is { } candidate
                    && IsInOperationFolder(candidate, operation))
                {
                    run.RetryOwnership = CookOutputLease.TryAcquireRetryInput(operation.Project.ProjectRoot, operation.OperationId)
                        ?? throw new CookOutputBusyException("The retry input is already owned.");
                }
            }
        }
        finally
        {
            ownership?.Dispose();
        }
    }

    private void ReleaseRetryInputsIfClosed()
    {
        List<FileStream> release = [];
        lock (this.stateLock)
        {
            if (this.activeOperation is not null)
            {
                return;
            }

            foreach (var run in this.runs.Values)
            {
                if (run.RetryOwnership is { } ownership
                    && (this.disposed || !ReferenceEquals(run.Snapshot.Request.OriginContext, this.project)))
                {
                    release.Add(ownership);
                    run.RetryOwnership = null;
                }
            }
        }

        foreach (var ownership in release)
        {
            ownership.Dispose();
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Observe and log cancellation callback failures while always releasing the retired project token.")]
    private async Task CancelLifetimeAsync(CancellationTokenSource cancellation)
    {
        try
        {
            await cancellation.CancelAsync().ConfigureAwait(false);
        }
        catch (Exception ex)
        {
            this.LogLifetimeFailure(ex);
        }
        finally
        {
            cancellation.Dispose();
        }
    }
}
