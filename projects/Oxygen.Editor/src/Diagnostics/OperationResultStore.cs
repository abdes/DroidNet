// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Core.Diagnostics;

namespace Oxygen.Editor.Diagnostics;

/// <summary>
/// Host-level in-memory operation result store.
/// </summary>
public sealed class OperationResultStore : IOperationResultStore
{
    private readonly Lock syncLock = new();
    private readonly List<OperationResult> results = [];
    private readonly List<IObserver<OperationResult>> observers = [];
    private readonly int capacity;

    /// <summary>
    /// Initializes a new instance of the <see cref="OperationResultStore"/> class.
    /// </summary>
    /// <param name="capacity">Maximum number of retained results.</param>
    public OperationResultStore(int capacity = 512)
    {
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(capacity);
        this.capacity = capacity;
    }

    /// <inheritdoc/>
    public OperationResult? Latest
    {
        get
        {
            lock (this.syncLock)
            {
                return this.results.Count == 0 ? null : this.results[^1];
            }
        }
    }

    /// <inheritdoc/>
    public void Publish(OperationResult result)
    {
        ArgumentNullException.ThrowIfNull(result);

        IObserver<OperationResult>[] snapshot;
        lock (this.syncLock)
        {
            if (this.results.Count == this.capacity)
            {
                this.results.RemoveAt(0);
            }

            this.results.Add(result);
            snapshot = [.. this.observers];
        }

        foreach (var observer in snapshot)
        {
            observer.OnNext(result);
        }
    }

    /// <inheritdoc/>
    public IDisposable Subscribe(IObserver<OperationResult> observer)
    {
        ArgumentNullException.ThrowIfNull(observer);

        lock (this.syncLock)
        {
            this.observers.Add(observer);
        }

        return new Subscription(this, observer);
    }

    /// <inheritdoc/>
    public IReadOnlyList<OperationResult> GetSnapshot()
    {
        lock (this.syncLock)
        {
            return [.. this.results];
        }
    }

    /// <inheritdoc/>
    public IReadOnlyList<OperationResult> GetSnapshot(OperationResultScopeFilter? filter)
    {
        if (filter is null)
        {
            return this.GetSnapshot();
        }

        lock (this.syncLock)
        {
            return [.. this.results.Where(result => Matches(result, filter))];
        }
    }

    private static bool Matches(OperationResult result, OperationResultScopeFilter filter)
    {
        if (filter.ProjectId is { } projectId && result.AffectedScope.ProjectId != projectId)
        {
            return false;
        }

        if (!string.IsNullOrWhiteSpace(filter.OperationKind)
            && !string.Equals(result.OperationKind, filter.OperationKind, StringComparison.Ordinal))
        {
            return false;
        }

        return filter.Domain is not { } domain || result.Diagnostics.Any(diagnostic => diagnostic.Domain == domain);
    }

    private void Unsubscribe(IObserver<OperationResult> observer)
    {
        lock (this.syncLock)
        {
            _ = this.observers.Remove(observer);
        }
    }

    private sealed class Subscription(OperationResultStore store, IObserver<OperationResult> observer) : IDisposable
    {
        private OperationResultStore? store = store;

        public void Dispose()
        {
            var target = Interlocked.Exchange(ref this.store, null);
            target?.Unsubscribe(observer);
        }
    }
}
