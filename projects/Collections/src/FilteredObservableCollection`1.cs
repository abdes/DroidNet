// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections;
using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.ComponentModel;

namespace DroidNet.Collections;

/// <summary>
///     A filtered, read-only view over an <see cref="ObservableCollection{T}"/>. Keeps itself in
///     sync with the source collection and with item property changes. The view only contains items
///     that satisfy the provided filter predicate and preserves the ordering of the source
///     collection.
/// </summary>
/// <typeparam name="T">The item type.</typeparam>
public sealed partial class FilteredObservableCollection<T> : IReadOnlyList<T>, IList<T>, IList, INotifyCollectionChanged, IDisposable
    where T : class, IEquatable<T>
{
    private readonly ObservableCollection<T> source;
    private readonly Predicate<T> filter;
    private readonly IFilteredViewBuilder<T>? viewBuilder;
    private readonly Dictionary<T, FilteredTree.FilteredNode> instanceMap;

    private readonly TimeSpan propertyChangedDebounceInterval;
    private readonly SynchronizationContext? synchronizationContext;
    private readonly Lock debounceGate = new();

    private readonly FilteredObservableCollectionOptions options;
    private readonly HashSet<T> propertySubscribedItems;

    private readonly HashSet<T> dirtyItems = new(ReferenceEqualityComparer.Instance);

    private readonly bool debounceEnabled;

    private FilteredTree tree;

    private int suspendCount;
    private Timer? debounceTimer;
    private bool debouncePending;
    private bool disposed;

    /// <summary>
    ///     Initializes a new instance of the <see cref="FilteredObservableCollection{T}"/> class
    ///     with the specified source collection, filter predicate, and options.
    /// </summary>
    /// <param name="source">The source <see cref="ObservableCollection{T}"/> to create a filtered view over.</param>
    /// <param name="filter">A predicate that determines which items from the source should be included in the filtered view.</param>
    /// <param name="options">Optional configuration options for the filtered collection. If <see langword="null"/>, default options are used.</param>
    /// <exception cref="ArgumentNullException">Thrown when <paramref name="source"/> or <paramref name="filter"/> is <see langword="null"/>.</exception>
    internal FilteredObservableCollection(
        ObservableCollection<T> source,
        Predicate<T> filter,
        FilteredObservableCollectionOptions? options = null)
        : this(source, filter, viewBuilder: null, options)
    {
    }

    /// <summary>
    ///     Initializes a new instance of the <see cref="FilteredObservableCollection{T}"/> class
    ///     with the specified source collection, filter predicate, view builder, and options.
    /// </summary>
    /// <param name="source">The source <see cref="ObservableCollection{T}"/> to create a filtered view over.</param>
    /// <param name="filter">A predicate that determines which items from the source should be included in the filtered view.</param>
    /// <param name="viewBuilder">An optional view builder that can add additional dependent items to the filtered view based on trigger items.</param>
    /// <param name="options">Optional configuration options for the filtered collection. If <see langword="null"/>, default options are used.</param>
    /// <exception cref="ArgumentNullException">Thrown when <paramref name="source"/> or <paramref name="filter"/> is <see langword="null"/>.</exception>
    internal FilteredObservableCollection(
        ObservableCollection<T> source,
        Predicate<T> filter,
        IFilteredViewBuilder<T>? viewBuilder,
        FilteredObservableCollectionOptions? options)
    {
        this.source = source ?? throw new ArgumentNullException(nameof(source));
        this.filter = filter ?? throw new ArgumentNullException(nameof(filter));
        this.viewBuilder = viewBuilder;

        var resolvedOptions = options ?? FilteredObservableCollectionOptions.Default;
        this.options = resolvedOptions;

        this.propertyChangedDebounceInterval = resolvedOptions.PropertyChangedDebounceInterval;
        this.debounceEnabled = this.propertyChangedDebounceInterval > TimeSpan.Zero;
        this.synchronizationContext = SynchronizationContext.Current;

        this.instanceMap = new Dictionary<T, FilteredTree.FilteredNode>(ReferenceEqualityComparer.Instance);
        this.tree = new FilteredTree(new SourceOrderComparer(source), this.GetNode);
        this.propertySubscribedItems = new HashSet<T>(ReferenceEqualityComparer.Instance);

        this.source.CollectionChanged += this.OnSourceCollectionChanged;
        this.options.ObservedProperties.CollectionChanged += this.OnObservedPropertiesCollectionChanged;

        this.RebuildAllFromSource();
    }

    /// <summary>
    ///     Occurs when the collection changes, either by adding or removing items.
    /// </summary>
    public event NotifyCollectionChangedEventHandler? CollectionChanged;

    /// <summary>
    ///     Occurs when a property value changes.
    /// </summary>
    public event EventHandler<PropertyChangedEventArgs>? PropertyChanged;

    /// <summary>
    ///     Gets the number of items in the filtered view.
    /// </summary>
    public int Count => this.disposed ? 0 : this.tree.IncludedCount;

    /// <summary>
    ///     Gets the item at the specified index in the filtered view.
    /// </summary>
    /// <param name="index">The zero-based index of the item to get.</param>
    /// <returns>The item at the specified index.</returns>
    /// <exception cref="ArgumentOutOfRangeException">
    ///     Thrown when <paramref name="index"/> is less than 0 or greater than or equal to <see cref="Count"/>.
    /// </exception>
    public T this[int index] => this.tree.SelectIncluded(index);

    /// <summary>
    ///     Determines whether the filtered view contains a specific item.
    /// </summary>
    /// <param name="item">The item to locate in the filtered view.</param>
    /// <returns><see langword="true"/> if the item is found in the filtered view; otherwise, <see langword="false"/>.</returns>
    public bool Contains(T item) => this.instanceMap.TryGetValue(item, out var node) && node.Included;

    /// <summary>
    ///     Determines the index of a specific item in the filtered view.
    /// </summary>
    /// <param name="item">The item to locate in the filtered view.</param>
    /// <returns>The index of the item if found in the filtered view; otherwise, -1.</returns>
    public int IndexOf(T item)
        => this.instanceMap.TryGetValue(item, out var node) && node.Included
            ? FilteredObservableCollection<T>.FilteredTree.RankIncluded(node)
            : -1;

    /// <summary>
    ///     Returns an enumerator that iterates through the filtered view.
    /// </summary>
    /// <returns>An enumerator that can be used to iterate through the filtered view.</returns>
    public IEnumerator<T> GetEnumerator() => this.tree.GetIncludedEnumerator(this.instanceMap);

    /// <inheritdoc />
    IEnumerator IEnumerable.GetEnumerator() => this.GetEnumerator();

    /// <summary>
    ///     Defers collection change notifications until the returned <see cref="IDisposable"/> is disposed.
    ///     This allows multiple changes to be batched together, resulting in a single reset notification.
    /// </summary>
    /// <returns>An <see cref="IDisposable"/> that, when disposed, resumes notifications and triggers a collection reset.</returns>
    /// <exception cref="ObjectDisposedException">Thrown when the collection has been disposed.</exception>
    public IDisposable DeferNotifications()
    {
        ObjectDisposedException.ThrowIf(this.disposed, nameof(FilteredObservableCollection<>));
        this.suspendCount++;
        return new DeferHelper(this);
    }

    /// <summary>
    ///     Forces a complete rebuild of the filtered view from the source collection.
    ///     If notifications are currently deferred, the rebuild is performed silently.
    /// </summary>
    public void Refresh()
    {
        if (this.disposed)
        {
            return;
        }

        if (this.suspendCount > 0)
        {
            this.RebuildAllFromSource(suppressEvents: true);
            return;
        }

        this.RebuildAllFromSource(suppressEvents: false);
    }

    /// <summary>
    ///     Re-evaluates the filter predicate for all source items and applies the resulting changes
    ///     incrementally (without emitting a Reset).
    /// </summary>
    /// <remarks>
    ///     This is intended for scenarios where the predicate logic changes (for example, the UI
    ///     switches a filter mode). It computes the required delta across the whole source and then
    ///     applies it using the same delta pipeline as property-change driven updates.
    /// </remarks>
    public void ReevaluatePredicate()
    {
        if (this.disposed)
        {
            return;
        }

        if (this.suspendCount > 0)
        {
            this.RebuildAllFromSource(suppressEvents: true);
            return;
        }

        var delta = new Delta();
        var pendingNodeState = new Dictionary<T, PendingNodeState>(ReferenceEqualityComparer.Instance);
        var processedAnything = false;

        foreach (var item in this.source)
        {
            if (!this.instanceMap.TryGetValue(item, out var node))
            {
                continue;
            }

            if (this.TryAccumulatePropertyChangeDelta(item, node, delta, pendingNodeState))
            {
                processedAnything = true;
            }
        }

        if (!processedAnything)
        {
            return;
        }

        if (delta.Changes.Count > 0)
        {
            _ = this.ApplyDelta(delta, suppressEvents: false);
        }

        FilteredObservableCollection<T>.ApplyPendingNodeState(pendingNodeState);
    }

    /// <summary>
    ///     Releases all resources used by the <see cref="FilteredObservableCollection{T}"/>.
    ///     This includes unsubscribing from all event handlers and clearing internal state.
    /// </summary>
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;

        this.source.CollectionChanged -= this.OnSourceCollectionChanged;
        this.options.ObservedProperties.CollectionChanged -= this.OnObservedPropertiesCollectionChanged;

        foreach (var item in this.propertySubscribedItems)
        {
            if (item is INotifyPropertyChanged notify)
            {
                notify.PropertyChanged -= this.OnItemPropertyChanged;
            }
        }

        this.propertySubscribedItems.Clear();

        this.debounceTimer?.Dispose();
        this.instanceMap.Clear();

        this.tree = null!;
    }

    private static int IndexOfReference(IReadOnlyList<T> list, T item)
    {
        for (var i = 0; i < list.Count; i++)
        {
            if (ReferenceEquals(list[i], item))
            {
                return i;
            }
        }

        return -1;
    }

    private static void ApplyPendingNodeState(Dictionary<T, PendingNodeState> pending)
    {
        foreach (var kvp in pending)
        {
            var state = kvp.Value;
            if (state.HasPredicateMatchedUpdate)
            {
                state.Node.PredicateMatched = state.PredicateMatched;
            }

            if (state.HasCachedDependenciesUpdate)
            {
                state.Node.CachedDependencies = state.CachedDependencies;
            }
        }
    }

    private FilteredTree.FilteredNode GetNode(T item) => this.instanceMap[item];

    private void RebuildAllFromSource(bool suppressEvents = false)
    {
        this.tree = new FilteredTree(new SourceOrderComparer(this.source), this.GetNode);

        this.instanceMap.Clear();

        foreach (var item in this.propertySubscribedItems)
        {
            if (item is INotifyPropertyChanged notify)
            {
                notify.PropertyChanged -= this.OnItemPropertyChanged;
            }
        }

        this.propertySubscribedItems.Clear();

        foreach (var item in this.source)
        {
            this.AttachItem(item);
        }

        this.CalculateInclusionForBatch(this.source, suppressEvents: true);

        if (!suppressEvents)
        {
            this.OnCollectionChanged(new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Reset));
            this.OnPropertyChanged(nameof(this.Count));
            this.OnPropertyChanged("Item[]");
        }
    }

    private void AttachItem(T item)
    {
        if (this.instanceMap.ContainsKey(item))
        {
            return;
        }

        var node = new FilteredTree.FilteredNode(item);
        this.instanceMap[item] = node;

        /* Do not add to tree here; CalculateInclusionForBatch/ApplyDelta will add if included.*/

        if (item is INotifyPropertyChanged notify && this.ShouldObserveProperties())
        {
            notify.PropertyChanged += this.OnItemPropertyChanged;
            this.propertySubscribedItems.Add(item);
        }
    }

    private void DetachItem(T item)
    {
        if (this.instanceMap.TryGetValue(item, out var node))
        {
            // Nodes are only added to the tree when included (RefCount > 0).
            // Source-removal processing may already have removed the node via ApplyDelta.
            // Avoid double-removal and avoid removing nodes that were never added.
            if (node.Included)
            {
                this.tree.RemoveNode(node);
            }

            _ = this.instanceMap.Remove(item);

            if (this.propertySubscribedItems.Contains(item))
            {
                if (item is INotifyPropertyChanged notify)
                {
                    notify.PropertyChanged -= this.OnItemPropertyChanged;
                }

                _ = this.propertySubscribedItems.Remove(item);
            }
        }
    }

    private void OnSourceCollectionChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        if (this.disposed)
        {
            return;
        }

        if (this.suspendCount > 0)
        {
            this.RebuildAllFromSource(suppressEvents: true);
            return;
        }

        // Local handlers keep the method compact while preserving original logic.
        void HandleAdd()
        {
            if (e.NewItems == null)
            {
                return;
            }

            var added = e.NewItems.Cast<T>().ToList();
            foreach (var item in added)
            {
                this.AttachItem(item);
            }

            this.CalculateInclusionForBatch(added, suppressEvents: false);
        }

        void HandleRemove()
        {
            if (e.OldItems == null)
            {
                return;
            }

            var removed = e.OldItems.Cast<T>().ToList();
            this.ProcessSourceRemovals(removed);
        }

        void HandleReplace()
        {
            if (e.OldItems != null)
            {
                this.ProcessSourceRemovals([.. e.OldItems.Cast<T>()]);
            }

            if (e.NewItems == null)
            {
                return;
            }

            var added = e.NewItems.Cast<T>().ToList();
            foreach (var item in added)
            {
                this.AttachItem(item);
            }

            this.CalculateInclusionForBatch(added, suppressEvents: false);
        }

        void HandleMove()
        {
            if (e.OldItems?.Count == 1)
            {
                var item = (T)e.OldItems[0]!;
                if (this.instanceMap.TryGetValue(item, out var node) && node.Included)
                {
                    var oldIndex = FilteredTree.RankIncluded(node);
                    this.tree.RemoveNode(node);
                    this.tree.AddNode(node);
                    var newIndex = FilteredTree.RankIncluded(node);
                    this.OnCollectionChanged(new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Move, item, newIndex, oldIndex));
                }
            }
            else if (e.OldItems != null)
            {
                var movedItems = e.OldItems.Cast<T>().ToList();
                foreach (var item in movedItems)
                {
                    if (this.instanceMap.TryGetValue(item, out var node) && node.Included)
                    {
                        this.tree.RemoveNode(node);
                        this.tree.AddNode(node);
                    }
                }

                this.OnCollectionChanged(new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Reset));
            }
        }

        void HandleReset() => this.RebuildAllFromSource(suppressEvents: false);

        switch (e.Action)
        {
            case NotifyCollectionChangedAction.Add:
                HandleAdd();
                break;
            case NotifyCollectionChangedAction.Remove:
                HandleRemove();
                break;
            case NotifyCollectionChangedAction.Replace:
                HandleReplace();
                break;
            case NotifyCollectionChangedAction.Move:
                HandleMove();
                break;
            case NotifyCollectionChangedAction.Reset:
                HandleReset();
                break;
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "MA0051:Method is too long", Justification = "no logical split is obvious")]
    private void ProcessSourceRemovals(List<T> removedItems)
    {
        var delta = new Delta();
        var pendingNodeState = new Dictionary<T, PendingNodeState>(ReferenceEqualityComparer.Instance);
        var sawReferencedRemoval = false;

        foreach (var item in removedItems)
        {
            if (this.instanceMap.TryGetValue(item, out var node))
            {
                /* The source collection has already removed the item at this point.
                   Do not call the builder here. Instead, remove the contributions that were previously applied.*/

                if (!node.PredicateMatched && node.RefCount > 0)
                {
                    // The item is included only because other triggers reference it, but it is no longer in the source.
                    // Keep the view consistent with the source by forcing removal and reporting a contract violation.
                    sawReferencedRemoval = true;
                    delta.AddChange(item, -node.RefCount);
                }

                if (node.PredicateMatched)
                {
                    delta.AddChange(item, -1);
                }

                if (this.viewBuilder is not null && node.CachedDependencies is not null)
                {
                    foreach (var dep in node.CachedDependencies)
                    {
                        delta.AddChange(dep, -1);
                    }

                    PendingNodeState.For(pendingNodeState, item, node).SetCachedDependencies(value: null);
                }

                PendingNodeState.For(pendingNodeState, item, node).SetPredicateMatched(value: false);

                if (this.viewBuilder is not null)
                {
                    // Cleanse cached dependency sets on remaining nodes to ensure future deltas do not reference
                    // removed items.
                    foreach (var entry in this.instanceMap)
                    {
                        var otherNode = entry.Value;
                        var deps = otherNode.CachedDependencies;
                        if (deps is null || deps.Count == 0)
                        {
                            continue;
                        }

                        if (!deps.Contains(item))
                        {
                            continue;
                        }

                        var copy = new HashSet<T>(deps, ReferenceEqualityComparer.Instance);
                        _ = copy.Remove(item);
                        PendingNodeState.For(pendingNodeState, entry.Key, otherNode).SetCachedDependencies(copy);
                    }
                }
            }
        }

        _ = this.ApplyDelta(delta, suppressEvents: sawReferencedRemoval);
        FilteredObservableCollection<T>.ApplyPendingNodeState(pendingNodeState);

        foreach (var item in removedItems)
        {
            this.DetachItem(item);
        }

        if (sawReferencedRemoval)
        {
            throw new InvalidOperationException("A source item was removed while still referenced by the filtered view. This violates the builder contract.");
        }
    }

    private void CalculateInclusionForBatch(IEnumerable<T> items, bool suppressEvents)
    {
        var delta = new Delta();
        var pendingNodeState = new Dictionary<T, PendingNodeState>(ReferenceEqualityComparer.Instance);
        foreach (var item in items)
        {
            var predicateIncluded = this.filter(item);
            if (this.instanceMap.TryGetValue(item, out var node))
            {
                PendingNodeState.For(pendingNodeState, item, node).SetPredicateMatched(predicateIncluded);
            }

            if (predicateIncluded)
            {
                this.AccumulateDelta(delta, pendingNodeState, item, triggerIncluded: true, sourceList: this.source, negate: false);
            }
        }

        if (delta.Changes.Count > 0)
        {
            _ = this.ApplyDelta(delta, suppressEvents);
        }

        FilteredObservableCollection<T>.ApplyPendingNodeState(pendingNodeState);
    }

    private void OnItemPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (this.disposed || sender is not T item)
        {
            return;
        }

        if (this.debounceEnabled)
        {
            lock (this.debounceGate)
            {
                this.dirtyItems.Add(item);
                if (!this.debouncePending)
                {
                    this.debouncePending = true;
                    _ = this.debounceTimer?.Change(this.propertyChangedDebounceInterval, Timeout.InfiniteTimeSpan);
                    this.debounceTimer ??= new Timer(_ => this.ProcessDebounce(), state: null, this.propertyChangedDebounceInterval, Timeout.InfiniteTimeSpan);
                }
            }

            return;
        }

        if (!this.ShouldObserveProperties() || string.IsNullOrEmpty(e.PropertyName))
        {
            return;
        }

        if (!this.options.ObservedProperties.Contains(e.PropertyName, StringComparer.Ordinal))
        {
            return;
        }

        if (!this.instanceMap.TryGetValue(item, out var node))
        {
            return;
        }

        var delta = new Delta();

        var pendingNodeState = new Dictionary<T, PendingNodeState>(ReferenceEqualityComparer.Instance);

        if (!this.TryAccumulatePropertyChangeDelta(item, node, delta, pendingNodeState))
        {
            return;
        }

        if (delta.Changes.Count > 0)
        {
            _ = this.ApplyDelta(delta, suppressEvents: false);
        }

        FilteredObservableCollection<T>.ApplyPendingNodeState(pendingNodeState);
    }

    private void ProcessDebounce()
    {
        var callback = new SendOrPostCallback(_ =>
        {
            if (this.disposed)
            {
                return;
            }

            List<T> batch;
            lock (this.debounceGate)
            {
                batch = [.. this.dirtyItems];
                this.dirtyItems.Clear();
                this.debouncePending = false;
            }

            var delta = new Delta();
            var pendingNodeState = new Dictionary<T, PendingNodeState>(ReferenceEqualityComparer.Instance);
            var processedAnything = false;

            foreach (var item in batch)
            {
                if (!this.instanceMap.TryGetValue(item, out var node))
                {
                    continue;
                }

                if (this.TryAccumulatePropertyChangeDelta(item, node, delta, pendingNodeState))
                {
                    processedAnything = true;
                }
            }

            if (!processedAnything)
            {
                return;
            }

            if (delta.Changes.Count > 0)
            {
                _ = this.ApplyDelta(delta, suppressEvents: false);
            }

            FilteredObservableCollection<T>.ApplyPendingNodeState(pendingNodeState);
        });

        if (this.synchronizationContext != null)
        {
            this.synchronizationContext.Post(callback, state: null);
        }
        else
        {
            callback(state: null);
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "MA0051:Method is too long", Justification = "no logical split is obvious")]
    private bool TryAccumulatePropertyChangeDelta(
        T item,
        FilteredTree.FilteredNode node,
        Delta delta,
        Dictionary<T, PendingNodeState> pendingNodeState)
    {
        var wasMatched = node.PredicateMatched;
        var isMatched = this.filter(item);

        if (this.viewBuilder is null)
        {
            if (wasMatched == isMatched)
            {
                return false;
            }

            delta.AddChange(item, isMatched ? 1 : -1);
            PendingNodeState.For(pendingNodeState, item, node).SetPredicateMatched(isMatched);
            return true;
        }

        // Builder path
        if (!wasMatched && !isMatched)
        {
            // Nothing to do; avoid generating a spurious Reset.
            return false;
        }

        var oldDependencies = node.CachedDependencies;

        var nodeUpdate = PendingNodeState.For(pendingNodeState, item, node);

        if (wasMatched && !isMatched)
        {
            if (oldDependencies is not null)
            {
                foreach (var dep in oldDependencies)
                {
                    delta.AddChange(dep, -1);
                }
            }

            delta.AddChange(item, -1);
            nodeUpdate.SetCachedDependencies(value: null);
            nodeUpdate.SetPredicateMatched(value: false);
            return true;
        }

        if (!wasMatched && isMatched)
        {
            delta.AddChange(item, 1);
            var depsNew = this.viewBuilder.BuildForChangedItem(item, becameIncluded: true, this.source);
            this.ValidateBuilderDependencies(depsNew, this.source);
            nodeUpdate.SetCachedDependencies(depsNew);
            foreach (var dep in depsNew)
            {
                delta.AddChange(dep, 1);
            }

            nodeUpdate.SetPredicateMatched(value: true);
            return true;
        }

        // wasMatched && isMatched
        var updatedDependencies = this.viewBuilder.BuildForChangedItem(item, becameIncluded: true, this.source);
        this.ValidateBuilderDependencies(updatedDependencies, this.source);
        nodeUpdate.SetCachedDependencies(updatedDependencies);

        if (oldDependencies is null)
        {
            foreach (var dep in updatedDependencies)
            {
                delta.AddChange(dep, 1);
            }
        }
        else
        {
            var updatedLookup = new HashSet<T>(updatedDependencies, ReferenceEqualityComparer.Instance);
            foreach (var dep in oldDependencies)
            {
                if (!updatedLookup.Contains(dep))
                {
                    delta.AddChange(dep, -1);
                }
            }

            var oldLookup = new HashSet<T>(oldDependencies, ReferenceEqualityComparer.Instance);
            foreach (var dep in updatedDependencies)
            {
                if (!oldLookup.Contains(dep))
                {
                    delta.AddChange(dep, 1);
                }
            }
        }

        nodeUpdate.SetPredicateMatched(value: true);
        return true;
    }

    private void AccumulateDelta(
        Delta delta,
        Dictionary<T, PendingNodeState> pendingNodeState,
        T item,
        bool triggerIncluded,
        IReadOnlyList<T> sourceList,
        bool negate = false)
    {
        var change = triggerIncluded ? 1 : -1;
        if (negate)
        {
            change = -change;
        }

        delta.AddChange(item, change);

        if (this.viewBuilder != null)
        {
            _ = this.instanceMap.TryGetValue(item, out var node);
            IReadOnlySet<T> dependencies;
            if (negate && triggerIncluded && node?.CachedDependencies != null)
            {
                dependencies = node.CachedDependencies;
                PendingNodeState.For(pendingNodeState, item, node).SetCachedDependencies(value: null);
            }
            else
            {
                dependencies = this.viewBuilder.BuildForChangedItem(item, triggerIncluded, sourceList);
                this.ValidateBuilderDependencies(dependencies, sourceList);
                if (!negate && triggerIncluded && node != null)
                {
                    PendingNodeState.For(pendingNodeState, item, node).SetCachedDependencies(dependencies);
                }
            }

            foreach (var dep in dependencies)
            {
                delta.AddChange(dep, change);
            }
        }
    }

    private bool ApplyDelta(Delta delta, bool suppressEvents)
    {
        var plans = BuildPlans(delta);

        var removals = plans
            .Where(p => p.WasIncluded && !p.IsIncluded)
            .OrderByDescending(p => p.RemovalIndex)
            .ThenByDescending(p => p.SourceIndex)
            .ToList();

        var additions = plans
            .Where(p => !p.WasIncluded && p.IsIncluded)
            .OrderBy(p => p.SourceIndex)
            .ToList();

        var updates = plans
            .Where(p => p.WasIncluded == p.IsIncluded)
            .ToList();

        var anyStructuralChanges = removals.Count > 0 || additions.Count > 0;

        if (suppressEvents)
        {
            ApplyWithoutEvents(removals, additions, updates);
            return anyStructuralChanges;
        }

        ApplyRemovalsWithEvents(removals);
        ApplyAdditionsWithEvents(additions);
        ApplyUpdates(updates);

        if (anyStructuralChanges)
        {
            this.OnPropertyChanged(nameof(this.Count));
            this.OnPropertyChanged("Item[]");
        }

        return anyStructuralChanges;

        List<DeltaPlanEntry> BuildPlans(Delta deltaToPlan)
        {
            var planned = new List<DeltaPlanEntry>();

            foreach (var kvp in deltaToPlan.Changes)
            {
                var item = kvp.Key;
                var change = kvp.Value;

                if (!this.instanceMap.TryGetValue(item, out var node))
                {
                    throw new InvalidOperationException("Delta contains an item that is not present in the source instance map. This violates the builder contract.");
                }

                var oldRef = node.RefCount;
                var newRef = oldRef + change;
                if (newRef < 0)
                {
                    throw new InvalidOperationException("Delta would cause a negative reference count. This violates the builder contract.");
                }

                var wasIncluded = oldRef > 0;
                var isIncluded = newRef > 0;

                var sourceIndex = GetSourceIndexOrMax(item);

                int? removalIndex = null;
                if (wasIncluded && !isIncluded)
                {
                    // Capture the index from the pre-change tree state.
                    removalIndex = FilteredObservableCollection<T>.FilteredTree.RankIncluded(node);
                    removalIndex = FilteredTree.RankIncluded(node);
                }

                planned.Add(new DeltaPlanEntry(item, node, oldRef, newRef, wasIncluded, isIncluded, sourceIndex, removalIndex));
            }

            return planned;
        }

        int GetSourceIndexOrMax(T item)
        {
            var sourceIndex = IndexOfReference(this.source, item);
            if (sourceIndex < 0)
            {
                // Deterministic fallback ordering for items that are no longer in source.
                return int.MaxValue;
            }

            return sourceIndex;
        }

        void ApplyWithoutEvents(List<DeltaPlanEntry> removalPlans, List<DeltaPlanEntry> additionPlans, List<DeltaPlanEntry> updatePlans)
        {
            foreach (var removal in removalPlans)
            {
                this.tree.RemoveNode(removal.Node);
                removal.Node.RefCount = removal.NewRef;
            }

            foreach (var addition in additionPlans)
            {
                addition.Node.RefCount = addition.NewRef;
                this.tree.AddNode(addition.Node);
            }

            foreach (var update in updatePlans)
            {
                update.Node.RefCount = update.NewRef;
            }
        }

        void ApplyRemovalsWithEvents(List<DeltaPlanEntry> removalPlans)
        {
            List<T>? runRemovedItemsDescending = null;
            var runStartingIndex = -1;
            var previousIndex = -1;

            void FlushRemovalRun()
            {
                if (runRemovedItemsDescending is null || runRemovedItemsDescending.Count == 0)
                {
                    return;
                }

                runRemovedItemsDescending.Reverse();
                this.OnCollectionChanged(new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Remove, runRemovedItemsDescending, runStartingIndex));
                runRemovedItemsDescending = null;
            }

            foreach (var removal in removalPlans)
            {
                var index = removal.RemovalIndex!.Value;

                if (runRemovedItemsDescending is not null && index != previousIndex - 1)
                {
                    FlushRemovalRun();
                }

                runRemovedItemsDescending ??= [];

                // Start index for a contiguous descending run is the lowest index encountered.
                runStartingIndex = index;
                previousIndex = index;

                // Apply the mutation before raising the event to keep Count consistent.
                this.tree.RemoveNode(removal.Node);
                removal.Node.RefCount = removal.NewRef;
                runRemovedItemsDescending.Add(removal.Item);
            }

            FlushRemovalRun();
        }

        void ApplyAdditionsWithEvents(List<DeltaPlanEntry> additionPlans)
        {
            List<T>? runAddedItems = null;
            var addRunStartingIndex = -1;

            void FlushAdditionRun()
            {
                if (runAddedItems is null || runAddedItems.Count == 0)
                {
                    return;
                }

                this.OnCollectionChanged(new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Add, runAddedItems, addRunStartingIndex));
                runAddedItems = null;
                addRunStartingIndex = -1;
            }

            foreach (var addition in additionPlans)
            {
                var insertionIndex = this.tree.Rank(addition.Item);

                if (runAddedItems is not null && insertionIndex != addRunStartingIndex + runAddedItems.Count)
                {
                    FlushAdditionRun();
                }

                if (runAddedItems is null)
                {
                    runAddedItems = [];
                    addRunStartingIndex = insertionIndex;
                }

                addition.Node.RefCount = addition.NewRef;
                this.tree.AddNode(addition.Node);
                runAddedItems.Add(addition.Item);
            }

            FlushAdditionRun();
        }

        void ApplyUpdates(List<DeltaPlanEntry> updatePlans)
        {
            foreach (var update in updatePlans)
            {
                update.Node.RefCount = update.NewRef;
            }
        }
    }

    private void ValidateBuilderDependencies(IReadOnlySet<T> dependencies, IReadOnlyList<T> sourceList)
    {
        foreach (var dep in dependencies)
        {
            if (!this.instanceMap.ContainsKey(dep))
            {
                throw new InvalidOperationException("Builder returned an item instance that is not tracked by the source instance map. Builders must return exact source instances.");
            }

            if (IndexOfReference(sourceList, dep) < 0)
            {
                throw new InvalidOperationException("Builder returned an item instance that is not present in the source collection.");
            }
        }
    }

    private void OnObservedPropertiesCollectionChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        this.UpdateSubscriptions();

        // Changing observed properties should not require a full rebuild of the view.
        // Subscriptions affect which future item changes we observe, not the current inclusion state.
        // Re-evaluate the predicate incrementally to reconcile any changes that may have occurred
        // while a property was not observed.
        this.ReevaluatePredicate();
    }

    private void UpdateSubscriptions()
    {
        foreach (var item in this.propertySubscribedItems)
        {
            if (item is INotifyPropertyChanged notify)
            {
                notify.PropertyChanged -= this.OnItemPropertyChanged;
            }
        }

        this.propertySubscribedItems.Clear();

        if (this.ShouldObserveProperties())
        {
            // Using source to ensure we cover all items potentially needing observation
            foreach (var item in this.source)
            {
                if (item is INotifyPropertyChanged notify)
                {
                    notify.PropertyChanged += this.OnItemPropertyChanged;
                    this.propertySubscribedItems.Add(item);
                }
            }
        }
    }

    private void OnCollectionChanged(NotifyCollectionChangedEventArgs e) => this.CollectionChanged?.Invoke(this, e);

    private void OnPropertyChanged(string propertyName) => this.PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));

    private bool ShouldObserveProperties() => this.options.ObservedProperties.Count > 0;

    private sealed record DeltaPlanEntry(
        T Item,
        FilteredTree.FilteredNode Node,
        int OldRef,
        int NewRef,
        bool WasIncluded,
        bool IsIncluded,
        int SourceIndex,
        int? RemovalIndex);

    private sealed class PendingNodeState
    {
        private PendingNodeState(FilteredTree.FilteredNode node)
        {
            this.Node = node;
        }

        public FilteredTree.FilteredNode Node { get; }

        public bool HasPredicateMatchedUpdate { get; private set; }

        public bool PredicateMatched { get; private set; }

        public bool HasCachedDependenciesUpdate { get; private set; }

        public IReadOnlySet<T>? CachedDependencies { get; private set; }

        public static PendingNodeState For(Dictionary<T, PendingNodeState> pending, T item, FilteredTree.FilteredNode node)
        {
            if (!pending.TryGetValue(item, out var state))
            {
                state = new PendingNodeState(node);
                pending[item] = state;
            }

            return state;
        }

        public void SetPredicateMatched(bool value)
        {
            this.HasPredicateMatchedUpdate = true;
            this.PredicateMatched = value;
        }

        public void SetCachedDependencies(IReadOnlySet<T>? value)
        {
            this.HasCachedDependenciesUpdate = true;
            this.CachedDependencies = value;
        }
    }

    private sealed class DeferHelper(FilteredObservableCollection<T> owner) : IDisposable
    {
        public void Dispose()
        {
            owner.suspendCount--;
            if (owner.suspendCount == 0)
            {
                owner.RebuildAllFromSource(suppressEvents: false);
            }
        }
    }

    private sealed class Delta
    {
        public Dictionary<T, int> Changes { get; } = new(ReferenceEqualityComparer.Instance);

        public void AddChange(T item, int change)
        {
            if (!this.Changes.TryGetValue(item, out var value))
            {
                value = 0;
            }

            this.Changes[item] = value + change;
        }
    }

    private sealed class FilteredTree : OrderStatisticTreeCollection<T>
    {
        private readonly Func<T, FilteredNode> nodeLookup;

        internal FilteredTree(IComparer<T> comparer, Func<T, FilteredNode> nodeLookup)
            : base(comparer)
        {
            this.nodeLookup = nodeLookup;
        }

        public int IncludedCount => this.Root?.SubtreeIncludedCount ?? 0;

        internal new FilteredNode? Root => base.Root as FilteredNode;

        public T SelectIncluded(int index)
        {
            if (index < 0 || index >= this.IncludedCount)
            {
                throw new ArgumentOutOfRangeException(nameof(index));
            }

            var x = this.Root;
            while (x != null)
            {
                var left = x.Left as FilteredNode;
                var leftCount = left?.SubtreeIncludedCount ?? 0;

                if (index < leftCount)
                {
                    x = left;
                }
                else
                {
                    var currentWeight = x.Included ? 1 : 0;
                    if (index < leftCount + currentWeight)
                    {
                        return x.Value;
                    }

                    index -= leftCount + currentWeight;
                    x = x.Right as FilteredNode;
                }
            }

            throw new InvalidOperationException("Filtered Select failed");
        }

        internal static int RankIncluded(FilteredNode node)
        {
            var rank = (node.Left as FilteredNode)?.SubtreeIncludedCount ?? 0;
            var y = node;
            while (y.Parent is FilteredNode parent)
            {
                if (y == parent.Right)
                {
                    rank += (parent.Included ? 1 : 0) + ((parent.Left as FilteredNode)?.SubtreeIncludedCount ?? 0);
                }

                y = parent;
            }

            return rank;
        }

        internal void AddNode(FilteredNode node)
        {
            node.Parent = null;
            node.Left = null;
            node.Right = null;
            node.SubtreeSize = 1;
            node.IsRed = true;
            this.Add(node.Value);
        }

        internal void RemoveNode(FilteredNode node) => this.DeleteNode(node);

        internal IEnumerator<T> GetIncludedEnumerator(Dictionary<T, FilteredNode> map)
        {
            foreach (var item in this)
            {
                if (map.TryGetValue(item, out var node) && node.Included)
                {
                    yield return item;
                }
            }
        }

        protected override Node CreateNode(T value) => this.nodeLookup(value);

        protected override void OnNodeUpdated(Node node)
        {
            if (node is FilteredNode fn)
            {
                fn.UpdateAugmentedData();
            }
        }

        internal sealed class FilteredNode(T value) : Node(value)
        {
            public int RefCount { get; set; }

            public bool Included => this.RefCount > 0;

            public bool PredicateMatched { get; set; }

            public int SubtreeIncludedCount { get; set; }

            public IReadOnlySet<T>? CachedDependencies { get; set; }

            // Hide the base properties to allow typed access? No, just cast.
            // But we need to use base.Left which is Node?. Cast it when needed.
            public void UpdateAugmentedData()
            {
                var left = this.Left as FilteredNode;
                var right = this.Right as FilteredNode;
                this.SubtreeIncludedCount = (this.Included ? 1 : 0) + (left?.SubtreeIncludedCount ?? 0) + (right?.SubtreeIncludedCount ?? 0);
            }
        }
    }

    private sealed class SourceOrderComparer(ObservableCollection<T> source) : IComparer<T>
    {
        private readonly ObservableCollection<T> source = source;

        [System.Diagnostics.CodeAnalysis.SuppressMessage("Style", "IDE0046:Convert to conditional expression", Justification = "code readability")]
        public int Compare(T? x, T? y)
        {
            if (ReferenceEquals(x, y))
            {
                return 0;
            }

            if (x is null)
            {
                return -1;
            }

            if (y is null)
            {
                return 1;
            }

            return this.IndexOfReference(x).CompareTo(this.IndexOfReference(y));
        }

        private int IndexOfReference(T value)
        {
            for (var i = 0; i < this.source.Count; i++)
            {
                if (ReferenceEquals(this.source[i], value))
                {
                    return i;
                }
            }

            return -1;
        }
    }
}
