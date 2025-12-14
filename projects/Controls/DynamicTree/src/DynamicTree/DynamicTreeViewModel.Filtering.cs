// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using System.ComponentModel;
using DroidNet.Collections;

namespace DroidNet.Controls;

/// <summary>
///     Filtering support for <see cref="DynamicTreeViewModel"/>.
/// </summary>
public abstract partial class DynamicTreeViewModel
{
    private readonly HashSet<ITreeItem> includedItems = [];
    private readonly HashSet<INotifyPropertyChanged> observedItems = [];

    private FilteredObservableCollection<ITreeItem>? filteredItems;
    private Predicate<ITreeItem>? filterPredicate;

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

            this.filterPredicate = value;
            this.OnPropertyChanged(nameof(this.FilterPredicate));
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
    public IEnumerable<ITreeItem> FilteredItems => this.GetOrCreateFilteredItems();

    /// <summary>
    ///     Recomputes the filter closure and refreshes <see cref="FilteredItems"/>.
    /// </summary>
    public void RefreshFiltering()
    {
        var view = this.filteredItems;
        if (view is null)
        {
            return;
        }

        this.RecomputeIncludedItems();
        view.Refresh();
    }

    private FilteredObservableCollection<ITreeItem> GetOrCreateFilteredItems()
    {
        if (this.filteredItems is not null)
        {
            return this.filteredItems;
        }

        // Manual-refresh view: the view model owns all updates (hierarchical closure is global).
        this.filteredItems = new FilteredObservableCollection<ITreeItem>(
            this.shownItems,
            item => this.includedItems.Contains(item),
            relevantProperties: null,
            observeSourceChanges: false,
            observeItemChanges: false);

        this.shownItems.CollectionChanged += this.OnShownItemsCollectionChangedForFiltering;
        foreach (var item in this.shownItems)
        {
            this.TryObserveItemForFiltering(item);
        }

        this.RecomputeIncludedItems();
        this.filteredItems.Refresh();

        return this.filteredItems;
    }

    private void OnShownItemsCollectionChangedForFiltering(object? sender, NotifyCollectionChangedEventArgs args)
    {
        _ = sender; // unused

        switch (args.Action)
        {
            case NotifyCollectionChangedAction.Add:
                if (args.NewItems is not null)
                {
                    foreach (var item in args.NewItems.OfType<ITreeItem>())
                    {
                        this.TryObserveItemForFiltering(item);
                    }
                }

                break;

            case NotifyCollectionChangedAction.Remove:
                if (args.OldItems is not null)
                {
                    foreach (var item in args.OldItems.OfType<ITreeItem>())
                    {
                        this.TryUnobserveItemForFiltering(item);
                    }
                }

                break;

            case NotifyCollectionChangedAction.Replace:
                if (args.OldItems is not null)
                {
                    foreach (var item in args.OldItems.OfType<ITreeItem>())
                    {
                        this.TryUnobserveItemForFiltering(item);
                    }
                }

                if (args.NewItems is not null)
                {
                    foreach (var item in args.NewItems.OfType<ITreeItem>())
                    {
                        this.TryObserveItemForFiltering(item);
                    }
                }

                break;

            case NotifyCollectionChangedAction.Reset:
            case NotifyCollectionChangedAction.Move:
            default:
                // For Reset/Move we rebuild filtering state from scratch.
                this.ResetObservedItemsForFiltering();
                break;
        }

        this.RefreshFiltering();
    }

    private void OnShownItemPropertyChangedForFiltering(object? sender, PropertyChangedEventArgs args)
    {
        _ = sender; // unused
        _ = args; // unused

        // Any property may affect the match predicate and/or the inclusion closure.
        this.RefreshFiltering();
    }

    private void TryObserveItemForFiltering(ITreeItem item)
    {
        if (item is INotifyPropertyChanged notify && this.observedItems.Add(notify))
        {
            notify.PropertyChanged += this.OnShownItemPropertyChangedForFiltering;
        }
    }

    private void TryUnobserveItemForFiltering(ITreeItem item)
    {
        if (item is INotifyPropertyChanged notify && this.observedItems.Remove(notify))
        {
            notify.PropertyChanged -= this.OnShownItemPropertyChangedForFiltering;
        }
    }

    private void ResetObservedItemsForFiltering()
    {
        foreach (var notify in this.observedItems)
        {
            notify.PropertyChanged -= this.OnShownItemPropertyChangedForFiltering;
        }

        this.observedItems.Clear();

        foreach (var item in this.shownItems)
        {
            this.TryObserveItemForFiltering(item);
        }
    }

    private void RecomputeIncludedItems()
    {
        this.includedItems.Clear();

        if (this.filterPredicate is null)
        {
            foreach (var item in this.shownItems)
            {
                this.includedItems.Add(item);
            }

            return;
        }

        // Hierarchy closure: include each matching item and all its ancestors within the current
        // expanded (shown) view. We compute this in one forward pass by tracking the current path
        // (index per depth) as we traverse the pre-order list.
        var include = new bool[this.shownItems.Count];
        var pathIndices = new List<int>();

        for (var index = 0; index < this.shownItems.Count; index++)
        {
            var item = this.shownItems[index];

            // Depth is expected to be >= 0 for visible items. Be defensive for hidden roots.
            var depth = item.Depth < 0 ? 0 : item.Depth;

            // Ensure pathIndices has room for this depth.
            while (pathIndices.Count > depth + 1)
            {
                pathIndices.RemoveAt(pathIndices.Count - 1);
            }

            while (pathIndices.Count < depth + 1)
            {
                pathIndices.Add(-1);
            }

            pathIndices[depth] = index;

            if (this.filterPredicate(item))
            {
                for (var d = 0; d <= depth; d++)
                {
                    var ancestorIndex = pathIndices[d];
                    if (ancestorIndex >= 0)
                    {
                        include[ancestorIndex] = true;
                    }
                }
            }
        }

        for (var index = 0; index < include.Length; index++)
        {
            if (include[index])
            {
                this.includedItems.Add(this.shownItems[index]);
            }
        }
    }
}
