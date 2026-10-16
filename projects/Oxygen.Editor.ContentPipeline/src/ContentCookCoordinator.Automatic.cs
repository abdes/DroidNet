// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Cooking;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Prioritizes explicit and preview work while retaining one project writer.</summary>
/// <remarks>
/// When foreground work waits while a cook on change holds the writer, the cook on change is cancelled. It stops at its
/// next cancellation point, which never publishes a partial result, ends as stopped and reports
/// <see cref="CookPreemptedException"/> to its callers, which cook the source again after the foreground work if it is
/// still stale. A cook already publishing is left to finish.
/// </remarks>
public sealed partial class ContentCookCoordinator
{
    private readonly List<WriterRequest> waitingWriters = [];
    private readonly List<TaskCompletionSource> idleWaiters = [];

    // Called under stateLock.
    private bool IsWriterIdle => this.activeOperation is null && this.waitingWriters.Count == 0;

    /// <inheritdoc />
    public Task WhenWriterIdleAsync(CancellationToken cancellationToken)
    {
        lock (this.stateLock)
        {
            if (this.IsWriterIdle)
            {
                return Task.CompletedTask;
            }

            var idle = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            this.idleWaiters.Add(idle);
            return idle.Task.WaitAsync(cancellationToken);
        }
    }

    private async Task AcquireWriterAsync(ContentCookOperation operation, CookRunRequest? request, SharedCook? shared, CancellationToken cancellationToken)
    {
        var waiting = new WriterRequest(operation, request, shared, cancellationToken);
        lock (this.stateLock)
        {
            this.waitingWriters.Add(waiting);
            this.DispatchNextWriter();
        }

        // Cancellation removes only a waiter. Once granted, RunCore owns release,
        // including cancellation between the grant and continuation resumption.
        var registration = cancellationToken.Register(() => this.CancelWaitingWriter(waiting));
        await using var registrationLifetime = registration.ConfigureAwait(false);
        await waiting.Granted.Task.ConfigureAwait(false);
    }

    private void CancelWaitingWriter(WriterRequest waiting)
    {
        lock (this.stateLock)
        {
            if (this.waitingWriters.Remove(waiting))
            {
                _ = waiting.Granted.TrySetCanceled(waiting.CancellationToken);
                this.DispatchNextWriter();
            }
        }
    }

    // Queue mutation and writer grants occur under stateLock. Continuations run
    // asynchronously so neither input capture nor user callbacks own this lock.
    private void DispatchNextWriter()
    {
        for (var index = this.waitingWriters.Count - 1; index >= 0; index--)
        {
            var waiting = this.waitingWriters[index];
            if (this.disposed || waiting.Operation.ProjectLifetime != this.lifetime || waiting.CancellationToken.IsCancellationRequested)
            {
                this.waitingWriters.RemoveAt(index);
                _ = waiting.Granted.TrySetCanceled(new CancellationToken(canceled: true));
            }
        }

        if (this.activeOperation is not null)
        {
            this.PreemptAutomaticWriter();
            return;
        }

        if (this.waitingWriters.Count == 0)
        {
            this.ReleaseIdleWaiters();
            return;
        }

        var next = this.waitingWriters.FindIndex(static waiting => waiting.IsForeground);
        if (next < 0)
        {
            next = 0;
        }

        var granted = this.waitingWriters[next];
        this.waitingWriters.RemoveAt(next);
        this.activeOperation = granted.Operation;
        if (granted.Shared is { } shared)
        {
            this.RemovePendingShared(shared);
        }

        _ = granted.Granted.TrySetResult();
    }

    // Called under stateLock.
    private void ReleaseIdleWaiters()
    {
        foreach (var idle in this.idleWaiters)
        {
            _ = idle.TrySetResult();
        }

        this.idleWaiters.Clear();
    }

    // Called under stateLock.
    private void PreemptAutomaticWriter()
    {
        if (this.activeOperation is not { } active || !this.waitingWriters.Exists(static waiting => waiting.IsForeground)
            || !this.runs.TryGetValue(active.OperationId, out var run) || run.Preempted
            || run.Snapshot is not { Request: { IsAutomatic: true, IsDemand: false }, IsCompleted: false }
            || run.Snapshot.State is CookRunState.Publishing or CookRunState.Cancelling)
        {
            return;
        }

        run.Preempted = true;
        _ = run.Cancellation.CancelAsync();
    }

    private sealed class WriterRequest(ContentCookOperation operation, CookRunRequest? request, SharedCook? shared, CancellationToken cancellationToken)
    {
        public ContentCookOperation Operation { get; } = operation;

        public CookRunRequest? Request => this.Shared?.Request ?? request;

        public SharedCook? Shared { get; } = shared;

        public CancellationToken CancellationToken { get; } = cancellationToken;

        public bool IsForeground => this.Request?.IsAutomatic != true || this.Request.IsDemand;

        public TaskCompletionSource Granted { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);
    }
}
