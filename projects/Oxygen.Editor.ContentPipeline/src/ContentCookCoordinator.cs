// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Serializes cook writers and drains cancelled project lifetimes before releasing ownership.</summary>
public sealed partial class ContentCookCoordinator : IContentCookCoordinator, ICookRunService, IObserver<ProjectContext?>, IDisposable
{
    private readonly Lock stateLock = new();
    private readonly IProjectContextService projectContextService;
    private readonly ILogger<ContentCookCoordinator> logger;
    private readonly IDisposable subscription;
    private CancellationTokenSource lifetimeCancellation = new();
    private ProjectContext? project;
    private ContentCookOperation? activeOperation;
    private long lifetime;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="ContentCookCoordinator"/> class.</summary>
    /// <param name="projectContextService">The active project and activation stream.</param>
    /// <param name="logger">The coordinator logger.</param>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "MA0040:Use a cancellation token", Justification = "The subscription spans project lifetimes and is disposed by this coordinator, not by a single project's cancellation token.")]
    public ContentCookCoordinator(IProjectContextService projectContextService, ILogger<ContentCookCoordinator> logger)
    {
        this.projectContextService = projectContextService;
        this.logger = logger;
        this.subscription = projectContextService.ProjectChanged.Subscribe(this);
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
                || !ReferenceEquals(operation.Project, this.project)
                || !ReferenceEquals(operation.Project, this.projectContextService.ActiveProject))
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

        this.subscription.Dispose();
        _ = this.CancelLifetimeAsync(cancellation);
        this.ReleaseRetryInputsIfClosed();
    }

    /// <inheritdoc />
    void IObserver<ProjectContext?>.OnNext(ProjectContext? value)
    {
        CancellationTokenSource previous;
        lock (this.stateLock)
        {
            if (this.disposed)
            {
                return;
            }

            previous = this.lifetimeCancellation;
            this.lifetimeCancellation = new();
            this.project = value;
            this.lifetime++;
            this.DispatchNextWriter();
        }

        _ = this.CancelLifetimeAsync(previous);
        this.ReleaseRetryInputsIfClosed();
    }

    /// <inheritdoc />
    void IObserver<ProjectContext?>.OnCompleted() => ((IObserver<ProjectContext?>)this).OnNext(value: null);

    /// <inheritdoc />
    void IObserver<ProjectContext?>.OnError(Exception error)
    {
        this.LogLifetimeFailure(error);
        ((IObserver<ProjectContext?>)this).OnNext(value: null);
    }

    private static bool IsInOperationFolder(string path, ContentCookOperation operation)
    {
        var folder = Path.GetFullPath(Path.Combine(operation.Project.ProjectRoot, ".build", "cook", operation.OperationId.ToString("N")));
        return Path.GetFullPath(path).StartsWith(folder + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase);
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Reliability", "CA2025:Ensure tasks using 'IDisposable' instances complete before the instances are disposed", Justification = "ReleaseAfterDrain takes ownership of the request cancellation and the operation lease and disposes them when the drain ends; this method no longer uses or disposes them.")]
    private async Task<T> RunCoreAsync<T>(Func<ContentCookOperation, CancellationToken, Task<T>> work, CookRunRequest? request, CancellationToken cancellationToken, SharedCook? shared = null, bool verifyCurrentAfterWork = true)
    {
        ArgumentNullException.ThrowIfNull(work);
        cancellationToken.ThrowIfCancellationRequested();
        var (operation, requestCancellation) = this.CreateRequest(cancellationToken, shared);
        IProgress<CookRunProgress>? progress = null;
        var acquired = false;
        var retained = false;
        FileStream? operationOwnership = null;
        try
        {
            progress = request is null ? null : this.AddRun(operation, request, requestCancellation, shared);
            using var reporting = CookRunContext.Enter(progress);
            while (true)
            {
                await this.AcquireWriterAsync(operation, request, shared, requestCancellation.Token).ConfigureAwait(false);
                acquired = true;
                await this.BeginWorkAsync(operation, progress, requestCancellation.Token).ConfigureAwait(false);
                operationOwnership ??= CookOutputLease.AcquireOperation(operation.Project.ProjectRoot, operation.OperationId);
                progress?.Report(new(Message: "Checking saved inputs and dependencies.", State: CookRunState.Preparing));
                try
                {
                    var result = await work.Invoke(operation, requestCancellation.Token).ConfigureAwait(false);
                    await this.CompleteWorkAsync(operation, result, verifyCurrentAfterWork, requestCancellation.Token).ConfigureAwait(false);
                    return result;
                }
                catch (CookInputsNeedSaveException ex) when (progress is not null)
                {
                    acquired = false;
                    this.ReleaseWriter();
                    await this.BlockRun(operation.OperationId, ex.Documents).WaitAsync(requestCancellation.Token).ConfigureAwait(false);
                }
            }
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
            ? new CookPreemptedException("The cook on change stopped so that a requested cook could run first.", exception)
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

    private (ContentCookOperation operation, CancellationTokenSource cancellation) CreateRequest(CancellationToken cancellationToken, SharedCook? shared = null)
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
            var operation = new ContentCookOperation(Guid.NewGuid(), context, this.lifetime);
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
