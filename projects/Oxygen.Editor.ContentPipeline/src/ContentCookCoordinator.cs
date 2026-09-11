// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Serializes cook writers and drains cancelled project lifetimes before releasing ownership.</summary>
public sealed partial class ContentCookCoordinator : IContentCookCoordinator, IObserver<ProjectContext?>, IDisposable
{
    private readonly Lock stateLock = new();
    private readonly SemaphoreSlim writer = new(1, 1);
    private readonly IProjectContextService projectContextService;
    private readonly ILogger<ContentCookCoordinator> logger;
    private readonly IDisposable subscription;
    private CancellationTokenSource lifetimeCancellation = new();
    private ProjectContext? project;
    private ContentCookOperation? activeOperation;
    private long lifetime;
    private int outstandingRequests;
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
    public async Task<T> RunAsync<T>(Func<ContentCookOperation, CancellationToken, Task<T>> work, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(work);
        cancellationToken.ThrowIfCancellationRequested();
        var (operation, requestCancellation) = this.CreateRequest(cancellationToken);
        var acquired = false;
        var retained = false;
        try
        {
            await this.writer.WaitAsync(requestCancellation.Token).ConfigureAwait(false);
            acquired = true;
            lock (this.stateLock)
            {
                this.activeOperation = operation;
            }

            requestCancellation.Token.ThrowIfCancellationRequested();
            this.VerifyCurrent(operation);
            var result = await work.Invoke(operation, requestCancellation.Token).ConfigureAwait(false);
            requestCancellation.Token.ThrowIfCancellationRequested();
            this.VerifyCurrent(operation);
            return result;
        }
        catch (ContentPipelineTerminationException ex) when (acquired)
        {
            retained = true;
            _ = ex.DrainCompletion.ContinueWith(
                completed =>
                {
                    _ = completed.Exception;
                    this.CompleteRequest(acquired: true, requestCancellation);
                },
                CancellationToken.None,
                TaskContinuationOptions.ExecuteSynchronously,
                TaskScheduler.Default);
            throw;
        }
        finally
        {
            if (!retained)
            {
                this.CompleteRequest(acquired, requestCancellation);
            }
        }
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
        bool disposeWriter;
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
            disposeWriter = this.outstandingRequests == 0;
        }

        this.subscription.Dispose();
        _ = this.CancelLifetimeAsync(cancellation);
        if (disposeWriter)
        {
            this.writer.Dispose();
        }
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
        }

        _ = this.CancelLifetimeAsync(previous);
    }

    /// <inheritdoc />
    void IObserver<ProjectContext?>.OnCompleted() => ((IObserver<ProjectContext?>)this).OnNext(value: null);

    /// <inheritdoc />
    void IObserver<ProjectContext?>.OnError(Exception error)
    {
        this.LogLifetimeFailure(error);
        ((IObserver<ProjectContext?>)this).OnNext(value: null);
    }

    private (ContentCookOperation operation, CancellationTokenSource cancellation) CreateRequest(CancellationToken cancellationToken)
    {
        lock (this.stateLock)
        {
            ObjectDisposedException.ThrowIf(this.disposed, this);
            var context = this.project ?? throw new InvalidOperationException("Cooking requires an active project.");
            var requestCancellation = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, this.lifetimeCancellation.Token);
            this.outstandingRequests++;
            return (new ContentCookOperation(Guid.NewGuid(), context, this.lifetime), requestCancellation);
        }
    }

    private void CompleteRequest(bool acquired, CancellationTokenSource requestCancellation)
    {
        requestCancellation.Dispose();
        if (acquired)
        {
            lock (this.stateLock)
            {
                this.activeOperation = null;
            }

            _ = this.writer.Release();
        }

        lock (this.stateLock)
        {
            this.outstandingRequests--;
            if (this.disposed && this.outstandingRequests == 0)
            {
                this.writer.Dispose();
            }
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

    [LoggerMessage(Level = LogLevel.Error, Message = "Content cook lifetime cancellation failed.")]
    private partial void LogLifetimeFailure(Exception exception);
}
