// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Collections.Immutable;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Exposes native acknowledgements without deriving readiness from paths or queued commands.</summary>
public sealed partial class EngineService
{
    private readonly ConcurrentQueue<RuntimeContentSnapshot> contentChanges = new();
    private readonly Lock contentStatusGate = new();
    private RuntimeContentSnapshot contentStatus = new(Guid.Empty, 0, RuntimeContentState.Unavailable, [], "The preview is not running.");
    private int publishingContentChanges;

    /// <inheritdoc />
    public event EventHandler<RuntimeContentChangedEventArgs>? ContentStatusChanged;

    /// <inheritdoc />
    public RuntimeContentSnapshot ContentStatus
    {
        get
        {
            var snapshot = Volatile.Read(ref this.contentStatus);
            return (this.State != EngineServiceState.Running || snapshot.RunId != this.commandDispatcher.RunId) && snapshot.State != RuntimeContentState.Unavailable
                ? snapshot with { State = RuntimeContentState.Unavailable, Roots = [], Reason = "The preview is not running." }
                : snapshot;
        }
    }

    private void ChangeContentStatus(RuntimeContentState state, ImmutableArray<string> roots, string? reason = null)
    {
        lock (this.contentStatusGate)
        {
            var previous = Volatile.Read(ref this.contentStatus);
            var next = new RuntimeContentSnapshot(this.commandDispatcher.RunId, previous.Revision + 1, state, roots, reason);
            Volatile.Write(ref this.contentStatus, next);
            this.contentChanges.Enqueue(next);
        }

        _ = Task.Run(this.PublishContentChanges, CancellationToken.None);
    }

    private void FailContentStatus(Exception exception)
        => this.ChangeContentStatus(this.State == EngineServiceState.Running ? RuntimeContentState.Failed : RuntimeContentState.Unavailable, [], exception.Message);

    private void PublishContentChanges()
    {
        do
        {
            if (Interlocked.CompareExchange(ref this.publishingContentChanges, 1, 0) != 0)
            {
                return;
            }

            try
            {
                while (this.contentChanges.TryDequeue(out var change))
                {
                    if (this.ContentStatusChanged is { } handlers)
                    {
                        var args = new RuntimeContentChangedEventArgs(change);
                        foreach (EventHandler<RuntimeContentChangedEventArgs> handler in handlers.GetInvocationList())
                        {
                            this.NotifySubscriber(() => handler(this, args));
                        }
                    }
                }
            }
            finally
            {
                Volatile.Write(ref this.publishingContentChanges, 0);
            }
        }
        while (!this.contentChanges.IsEmpty);
    }
}
