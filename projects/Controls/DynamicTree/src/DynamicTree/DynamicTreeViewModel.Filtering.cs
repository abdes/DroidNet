// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using System.ComponentModel;
using System.Diagnostics;
using System.Text;
using DroidNet.Collections;
using Microsoft.Extensions.Logging;

namespace DroidNet.Controls;

/// <summary>
///     Filtering support for <see cref="DynamicTreeViewModel"/>.
/// </summary>
public abstract partial class DynamicTreeViewModel
{
    /// <summary>
    ///     Minimum debounce interval for filtering-related property changes.
    /// </summary>
    public const int FilterDebounceMilliseconds = 250;

    // "Loaded-only" subtree filtering cache.
    private readonly Dictionary<ITreeItem, bool> filterSelfMatch = new(ReferenceEqualityComparer.Instance);
    private readonly Dictionary<ITreeItem, bool> filterSubtreeMatch = new(ReferenceEqualityComparer.Instance);
    private readonly HashSet<ITreeItem> filteringSubscribedNodes = new(ReferenceEqualityComparer.Instance);

    private FilteredObservableCollection<ITreeItem>? filteredItems;
    private HierarchicalFilterBuilder? filterBuilder;
    private Predicate<ITreeItem>? filterPredicate;
    private FilteredObservableCollectionOptions? filterOptions;
    private long filterCacheRevision;
    private long filterCacheComputedRevision;
    private bool filterCacheDirty;
    private CancellationTokenSource? filterCacheDebounceCts;
    private IReadOnlyList<string>? filteringRelevantProperties;

    /// <summary>
    ///     Gets or sets the current filter predicate used by <see cref="FilteredItems"/>.
    /// </summary>
    /// <remarks>
    ///     When <see langword="null"/>, all items match.
    /// </remarks>
    public Predicate<ITreeItem>? FilterPredicate
    {
        get => this.filterPredicate;
        set
        {
            if (ReferenceEquals(this.filterPredicate, value))
            {
                return;
            }

            // If filtering is being disabled (pass-all), tear down the filtered view so we don't
            // invoke predicate/builder while the control is rendering the source directly.
            if (value is null && this.filteredItems is not null)
            {
                this.DisposeFilteringSubscriptions();
                this.filteredItems.CollectionChanged -= this.OnFilteredItemsCollectionChanged;
                this.filteredItems.Dispose();
                this.filteredItems = null;
                this.filterOptions = null;
                this.filteringRelevantProperties = null;
            }

            this.filterPredicate = value;
            this.OnPropertyChanged(nameof(this.FilterPredicate));

            // Update runtime options when a filtered view exists and filtering is active.
            if (this.filterOptions is not null && this.filterPredicate is not null)
            {
                var relevantProperties = this.GetFilteringRelevantProperties();
                this.filteringRelevantProperties = relevantProperties;

                this.filterOptions.ObservedProperties.Clear();
                if (relevantProperties is not null)
                {
                    foreach (var p in relevantProperties)
                    {
                        this.filterOptions.ObservedProperties.Add(p);
                    }
                }
            }

            if (this.filterPredicate is not null)
            {
                this.InvalidateFilterCache("FilterPredicate changed");
            }

            this.RefreshFiltering();
        }
    }

    /// <summary>
    ///     Gets a filtered view of <see cref="ShownItems"/>.
    /// </summary>
    /// <remarks>
    ///     This is a rendering/visibility projection only. It never mutates the underlying
    ///     <see cref="ShownItems"/> collection and must not affect operation semantics.
    /// </remarks>
    public IEnumerable<ITreeItem> FilteredItems
        => this.filterPredicate is null ? this.shownItems : this.GetOrCreateFilteredItems();

    /// <summary>
    ///     Recomputes the filter closure and refreshes <see cref="FilteredItems"/>.
    /// </summary>
    public void RefreshFiltering()
    {
        if (this.filterPredicate is null)
        {
            return;
        }

        var view = this.filteredItems;
        if (view is null)
        {
            return;
        }

        // Refreshing should recompute the loaded-only closure, then reevaluate the view.
        this.RecomputeFilterCacheNow(reason: "RefreshFiltering");
        view.ReevaluatePredicate();
    }

    /// <summary>
    ///     Returns the set of property names whose changes should cause the filtered view to refresh.
    ///     Override in concrete view models to supply application-specific properties. Returning
    ///     <see langword="null"/> means all properties are considered relevant.
    /// </summary>
    /// <returns>Property names relevant to filtering, or <see langword="null"/> to treat all as relevant.</returns>
    protected virtual IReadOnlyList<string>? GetFilteringRelevantProperties() => null;

    /// <summary>
    /// Update the filtering observation options at runtime.
    /// </summary>
    /// <param name="observedProperties">The set of item property names to observe; an empty collection means observe none.</param>
    protected void UpdateFilteringObservation(IReadOnlyList<string>? observedProperties)
    {
        if (this.filterOptions is null)
        {
            return;
        }

        this.filterOptions.ObservedProperties.Clear();
        if (observedProperties is not null)
        {
            foreach (var p in observedProperties)
            {
                this.filterOptions.ObservedProperties.Add(p);
            }
        }
    }

    private FilteredObservableCollection<ITreeItem> GetOrCreateFilteredItems()
    {
        if (this.filterPredicate is null)
        {
            throw new InvalidOperationException("FilteredItems was requested while FilterPredicate is null. The control should bind to ShownItems in the pass-all case.");
        }

        if (this.filteredItems is not null)
        {
            return this.filteredItems;
        }

        // Manual-refresh view: the view model owns all updates (hierarchical closure is global).
        this.filterBuilder ??= new HierarchicalFilterBuilder();

        var relevantProperties = this.GetFilteringRelevantProperties();
        this.filteringRelevantProperties = relevantProperties;

        // If there's no current filter predicate, keep ObservedProperties empty so item property changes are ignored.
        // We always observe source collection changes but only observe item property changes when the predicate requires it.
        var options = new FilteredObservableCollectionOptions { PropertyChangedDebounceInterval = TimeSpan.FromMilliseconds(FilterDebounceMilliseconds) };

        // Configure observed properties: empty => observe none; otherwise observe listed properties when predicate is active.
        options.ObservedProperties.Clear();
        if (this.filterPredicate is not null && relevantProperties is not null)
        {
            foreach (var p in relevantProperties)
            {
                options.ObservedProperties.Add(p);
            }
        }

        this.filterOptions = options;

        // Ensure the cache is ready before the view starts evaluating the predicate.
        this.RecomputeFilterCacheNow(reason: "CreateFilteredView");

        this.filteredItems = FilteredObservableCollectionFactory.FromBuilder(
            this.shownItems,
            this.EvaluateFilterIncludingLoadedSubtree,
            this.filterBuilder,
            options);

        this.filteredItems.CollectionChanged += this.OnFilteredItemsCollectionChanged;

        this.filteredItems.Refresh();

        return this.filteredItems;
    }

    private bool EvaluateFilterIncludingLoadedSubtree(ITreeItem item)
    {
        var predicate = this.filterPredicate;
        if (predicate is null)
        {
            return true;
        }

        // Fast path: use the precomputed closure when available.
        if (!this.filterCacheDirty && this.filterCacheComputedRevision == this.filterCacheRevision
            && this.filterSubtreeMatch.TryGetValue(item, out var cached))
        {
            return cached;
        }

        // Fallback: never force-load. When cache is stale/missing, treat subtree as no-match.
        // A scheduled recomputation will fill in the closure and then reevaluate the view.
        return predicate(item);
    }

    private void InvalidateFilterCache(string reason)
    {
        if (this.filterPredicate is null)
        {
            return;
        }

        this.filterCacheDirty = true;
        this.filterCacheRevision++;

        this.LogFilteringCacheInvalidated(this.filterCacheRevision, reason);

        this.ScheduleFilterCacheRecompute();
    }

    private void ScheduleFilterCacheRecompute()
    {
        if (this.filteredItems is null || this.filterPredicate is null)
        {
            return;
        }

        this.filterCacheDebounceCts?.Cancel();
        this.filterCacheDebounceCts?.Dispose();

        var cts = new CancellationTokenSource();
        this.filterCacheDebounceCts = cts;

        _ = this.DebouncedRecomputeAndRefreshAsync(cts.Token);
    }

    private async Task DebouncedRecomputeAndRefreshAsync(CancellationToken token)
    {
        try
        {
            await Task.Delay(FilterDebounceMilliseconds, token).ConfigureAwait(true);
        }
        catch (OperationCanceledException)
        {
            return;
        }

        if (token.IsCancellationRequested)
        {
            return;
        }

        if (this.filteredItems is null || this.filterPredicate is null)
        {
            return;
        }

        this.RecomputeFilterCacheNow(reason: "Debounced");
        this.filteredItems.ReevaluatePredicate();
    }

    private void RecomputeFilterCacheNow(string reason)
    {
        var predicate = this.filterPredicate;
        if (predicate is null)
        {
            return;
        }

        // Avoid recomputing if nothing changed.
        if (!this.filterCacheDirty && this.filterCacheComputedRevision == this.filterCacheRevision)
        {
            return;
        }

        var sw = Stopwatch.StartNew();

        this.filterCacheDirty = false;
        this.filterCacheComputedRevision = this.filterCacheRevision;
        this.filterSelfMatch.Clear();
        this.filterSubtreeMatch.Clear();

        var visitedNodes = 0;
        var visitedEdges = 0;

        foreach (var root in this.shownItems)
        {
            if (root is not null)
            {
                ComputeForRoot(root);
            }
        }

        sw.Stop();
        this.LogFilteringCacheRecomputed(this.filterCacheComputedRevision, reason, visitedNodes, visitedEdges, sw.ElapsedMilliseconds);

        void ComputeForRoot(ITreeItem root)
        {
            // Iterative post-order DFS over already-loaded edges only.
            var stack = new Stack<(ITreeItem node, int nextIndex, IReadOnlyList<ITreeItem>? children)>();
            stack.Push((root, 0, null));

            while (stack.Count > 0)
            {
                var (node, nextIndex, children) = stack.Pop();

                // If we already computed this subtree (node reachable from multiple shown roots), skip.
                if (children is null && this.filterSubtreeMatch.ContainsKey(node))
                {
                    continue;
                }

                if (children is null)
                {
                    this.SubscribeForFiltering(node);
                    var self = predicate(node);
                    this.filterSelfMatch[node] = self;
                    visitedNodes++;

                    if (!TryGetLoadedChildrenNonLoading(node, out children))
                    {
                        // Unloaded subtree is treated as no-match.
                        this.filterSubtreeMatch[node] = self;
                        continue;
                    }

                    nextIndex = 0;
                }

                if (nextIndex >= children.Count)
                {
                    var subtree = this.filterSelfMatch[node];
                    for (var i = 0; !subtree && i < children.Count; i++)
                    {
                        var child = children[i];
                        subtree = child is not null
                            && this.filterSubtreeMatch.TryGetValue(child, out var childSubtree)
                            && childSubtree;
                    }

                    this.filterSubtreeMatch[node] = subtree;
                    continue;
                }

                stack.Push((node, nextIndex + 1, children));

                var nextChild = children[nextIndex];
                visitedEdges++;
                if (nextChild is not null && !this.filterSubtreeMatch.ContainsKey(nextChild))
                {
                    stack.Push((nextChild, 0, null));
                }
            }
        }

        static bool TryGetLoadedChildrenNonLoading(ITreeItem node, out IReadOnlyList<ITreeItem> loaded)
        {
            if (node is ILoadedChildrenAccessor accessor && accessor.TryGetLoadedChildren(out loaded))
            {
                return true;
            }

            loaded = [];
            return false;
        }
    }

    private void SubscribeForFiltering(ITreeItem node)
    {
        if (!this.filteringSubscribedNodes.Add(node))
        {
            return;
        }

        node.ChildrenCollectionChanged += this.OnFilteringChildrenCollectionChanged;

        if (node is INotifyPropertyChanged npc)
        {
            npc.PropertyChanged += this.OnFilteringItemPropertyChanged;
        }
    }

    private void OnFilteringItemPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (this.filterPredicate is null)
        {
            return;
        }

        // Expand/collapse is purely a ShownItems concern; it must not invalidate the loaded-subtree cache.
        if (string.Equals(e.PropertyName, nameof(ITreeItem.IsExpanded), StringComparison.Ordinal))
        {
            return;
        }

        // Respect the existing "relevant properties" concept.
        var relevant = this.filteringRelevantProperties;
        if (relevant is null)
        {
            this.InvalidateFilterCache($"Item property changed: {e.PropertyName ?? "<any>"}");
            return;
        }

        if (string.IsNullOrEmpty(e.PropertyName))
        {
            this.InvalidateFilterCache("Item property changed: <any>");
            return;
        }

        for (var i = 0; i < relevant.Count; i++)
        {
            if (string.Equals(relevant[i], e.PropertyName, StringComparison.Ordinal))
            {
                this.InvalidateFilterCache($"Item property changed: {e.PropertyName}");
                return;
            }
        }
    }

    private void OnFilteringChildrenCollectionChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        if (this.filterPredicate is null)
        {
            return;
        }

        // Subscribe to newly loaded/added nodes.
        if (e.NewItems is not null)
        {
            foreach (var obj in e.NewItems)
            {
                if (obj is ITreeItem item)
                {
                    this.SubscribeForFiltering(item);
                }
            }
        }

        // For Reset (common on first load), rescan loaded children.
        if (e.Action == NotifyCollectionChangedAction.Reset && sender is ITreeItem node)
        {
            if (node is ILoadedChildrenAccessor accessor && accessor.TryGetLoadedChildren(out var children))
            {
                for (var i = 0; i < children.Count; i++)
                {
                    this.SubscribeForFiltering(children[i]);
                }
            }
        }

        // Unsubscribe removed nodes (tree items have a single parent).
        if (e.OldItems is not null
            && (e.Action == NotifyCollectionChangedAction.Remove || e.Action == NotifyCollectionChangedAction.Replace))
        {
            foreach (var obj in e.OldItems)
            {
                if (obj is ITreeItem item)
                {
                    this.UnsubscribeForFiltering(item);
                }
            }
        }

        this.InvalidateFilterCache($"Loaded children changed: {e.Action}");
    }

    private void UnsubscribeForFiltering(ITreeItem node)
    {
        if (!this.filteringSubscribedNodes.Remove(node))
        {
            return;
        }

        node.ChildrenCollectionChanged -= this.OnFilteringChildrenCollectionChanged;
        if (node is INotifyPropertyChanged npc)
        {
            npc.PropertyChanged -= this.OnFilteringItemPropertyChanged;
        }
    }

    private void DisposeFilteringSubscriptions()
    {
        this.filterCacheDebounceCts?.Cancel();
        this.filterCacheDebounceCts?.Dispose();
        this.filterCacheDebounceCts = null;

        foreach (var node in this.filteringSubscribedNodes)
        {
            node.ChildrenCollectionChanged -= this.OnFilteringChildrenCollectionChanged;
            if (node is INotifyPropertyChanged npc)
            {
                npc.PropertyChanged -= this.OnFilteringItemPropertyChanged;
            }
        }

        this.filteringSubscribedNodes.Clear();
        this.filterSelfMatch.Clear();
        this.filterSubtreeMatch.Clear();
        this.filterCacheDirty = false;
        this.filterCacheRevision = 0;
        this.filterCacheComputedRevision = 0;
    }

    private void OnFilteredItemsCollectionChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        if (!this.logger.IsEnabled(LogLevel.Trace))
        {
            return;
        }

        static string? FormatItems(System.Collections.IList? items)
        {
            if (items is null || items.Count == 0)
            {
                return null;
            }

            const int maxItemsToLog = 6;
            var max = items.Count < maxItemsToLog ? items.Count : maxItemsToLog;

            var sb = new StringBuilder();
            _ = sb.Append('[');
            for (var i = 0; i < max; i++)
            {
                if (i > 0)
                {
                    _ = sb.Append(", ");
                }

                if (items[i] is ITreeItem item)
                {
                    _ = sb.Append(item.Label);
                }
                else
                {
                    _ = sb.Append(items[i]?.ToString() ?? "<null>");
                }
            }

            if (items.Count > max)
            {
                _ = sb.Append(", …+");
                _ = sb.Append(items.Count - max);
            }

            _ = sb.Append(']');
            return sb.ToString();
        }

        this.LogFilteredItemsChanged(e, FormatItems(e.NewItems), FormatItems(e.OldItems));
    }

    private sealed class HierarchicalFilterBuilder : IFilteredViewBuilder<ITreeItem>
    {
        private static readonly IReadOnlySet<ITreeItem> Empty = new HashSet<ITreeItem>(ReferenceEqualityComparer.Instance);

        public IReadOnlySet<ITreeItem> BuildForChangedItem(ITreeItem changedItem, bool becameIncluded, IReadOnlyList<ITreeItem> source)
        {
            // The filtered collection applies the predicate result for changedItem itself.
            // Here we return only its lineage (ancestors) so they stay visible when any descendant matches.
            _ = becameIncluded;

            if (source.Count == 0)
            {
                return Empty;
            }

            var changedIndex = -1;
            for (var i = 0; i < source.Count; i++)
            {
                if (ReferenceEquals(source[i], changedItem))
                {
                    changedIndex = i;
                    break;
                }
            }

            if (changedIndex < 0)
            {
                return Empty;
            }

            var depth = source[changedIndex].Depth;
            depth = depth < 0 ? 0 : depth;
            if (depth == 0)
            {
                return Empty;
            }

            var expectedAncestorDepth = depth - 1;
            var lineage = new HashSet<ITreeItem>(ReferenceEqualityComparer.Instance);

            for (var i = changedIndex - 1; i >= 0 && expectedAncestorDepth >= 0; i--)
            {
                var d = source[i].Depth;
                d = d < 0 ? 0 : d;
                if (d == expectedAncestorDepth)
                {
                    _ = lineage.Add(source[i]);
                    expectedAncestorDepth--;
                }
            }

            return lineage.Count == 0 ? Empty : lineage;
        }
    }
}
