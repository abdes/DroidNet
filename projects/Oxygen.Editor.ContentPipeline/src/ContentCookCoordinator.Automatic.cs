// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using Oxygen.Editor.ContentPipeline.Cooking;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>Prioritizes explicit and preview work while retaining one project writer.</summary>
public sealed partial class ContentCookCoordinator
{
    private readonly List<WriterRequest> waitingWriters = [];
    private bool automaticCookingPaused;

    /// <inheritdoc />
    public event PropertyChangedEventHandler? PropertyChanged;

    /// <inheritdoc />
    public bool IsAutomaticCookingPaused
    {
        get
        {
            lock (this.stateLock)
            {
                return this.automaticCookingPaused;
            }
        }

        set
        {
            lock (this.stateLock)
            {
                if (value == this.automaticCookingPaused)
                {
                    return;
                }

                this.automaticCookingPaused = value;
                this.DispatchNextWriter();
            }

            this.PropertyChanged?.Invoke(this, new(nameof(this.IsAutomaticCookingPaused)));
        }
    }

    private async Task AcquireWriterAsync(ContentCookOperation operation, CookRunRequest? request, CancellationToken cancellationToken)
    {
        var waiting = new WriterRequest(operation, request, cancellationToken);
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

        if (this.activeOperation is not null || this.waitingWriters.Count == 0)
        {
            return;
        }

        var next = this.waitingWriters.FindIndex(waiting => this.CanStart(waiting) && waiting.IsForeground);
        if (next < 0)
        {
            next = this.waitingWriters.FindIndex(this.CanStart);
        }

        if (next >= 0)
        {
            var waiting = this.waitingWriters[next];
            this.waitingWriters.RemoveAt(next);
            this.activeOperation = waiting.Operation;
            _ = waiting.Granted.TrySetResult();
        }
    }

    private bool CanStart(WriterRequest waiting)
        => !this.automaticCookingPaused || waiting.Request?.IsAutomatic != true;

    private sealed class WriterRequest(ContentCookOperation operation, CookRunRequest? request, CancellationToken cancellationToken)
    {
        public ContentCookOperation Operation { get; } = operation;

        public CookRunRequest? Request { get; } = request;

        public CancellationToken CancellationToken { get; } = cancellationToken;

        public bool IsForeground => this.Request?.IsAutomatic != true || this.Request.IsDemand;

        public TaskCompletionSource Granted { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);
    }
}
