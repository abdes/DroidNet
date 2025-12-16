// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Collections;

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

    private FilteredObservableCollection<ITreeItem>? filteredItems;
    private HierarchicalFilterBuilder? filterBuilder;
    private Predicate<ITreeItem>? filterPredicate;
    private FilteredObservableCollectionOptions? filterOptions;

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

            // Update runtime options so enabling/disabling the predicate will toggle
            // whether the filtered view auto-refreshes.
            if (this.filterOptions is not null)
            {
                var relevantProperties = this.GetFilteringRelevantProperties();

                if (this.filterPredicate is null)
                {
                    // When no predicate is active, observe no item property changes (empty collection).
                    this.filterOptions.ObservedProperties.Clear();
                }
                else
                {
                    this.filterOptions.ObservedProperties.Clear();
                    if (relevantProperties is not null)
                    {
                        foreach (var p in relevantProperties)
                        {
                            this.filterOptions.ObservedProperties.Add(p);
                        }
                    }
                }
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

        view.Refresh();
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
        if (this.filteredItems is not null)
        {
            return this.filteredItems;
        }

        // Manual-refresh view: the view model owns all updates (hierarchical closure is global).
        this.filterBuilder ??= new HierarchicalFilterBuilder(this);

        var relevantProperties = this.GetFilteringRelevantProperties();

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

        this.filteredItems = FilteredObservableCollectionFactory.FromBuilder(
            this.shownItems,
            this.filterBuilder,
            options);

        this.filteredItems.Refresh();

        return this.filteredItems;
    }

    private sealed class HierarchicalFilterBuilder(DynamicTreeViewModel owner) : IFilteredViewBuilder<ITreeItem>
    {
        private readonly List<ITreeItem> buffer = [];
        private readonly List<int> pathIndices = [];

        public IReadOnlyList<ITreeItem> Build(IReadOnlyList<ITreeItem> source)
        {
            this.buffer.Clear();

            if (source.Count == 0)
            {
                return this.buffer;
            }

            var predicate = owner.filterPredicate;
            if (predicate is null)
            {
                this.CopyAll(source);
                return this.buffer;
            }

            var include = this.ComputeIncludeMask(source, predicate);
            this.AppendIncluded(source, include);

            return this.buffer;
        }

        private void CopyAll(IReadOnlyList<ITreeItem> source)
        {
            for (var i = 0; i < source.Count; i++)
            {
                this.buffer.Add(source[i]);
            }
        }

        private bool[] ComputeIncludeMask(IReadOnlyList<ITreeItem> source, Predicate<ITreeItem> predicate)
        {
            var include = new bool[source.Count];
            this.pathIndices.Clear();

            for (var index = 0; index < source.Count; index++)
            {
                var item = source[index];

                // Depth is expected to be >= 0 for visible items. Be defensive for hidden roots.
                var depth = item.Depth < 0 ? 0 : item.Depth;

                // Ensure pathIndices has room for this depth.
                while (this.pathIndices.Count > depth + 1)
                {
                    this.pathIndices.RemoveAt(this.pathIndices.Count - 1);
                }

                while (this.pathIndices.Count < depth + 1)
                {
                    this.pathIndices.Add(-1);
                }

                this.pathIndices[depth] = index;

                if (!predicate(item))
                {
                    continue;
                }

                for (var ancestorDepth = 0; ancestorDepth <= depth; ancestorDepth++)
                {
                    var ancestorIndex = this.pathIndices[ancestorDepth];
                    if (ancestorIndex >= 0)
                    {
                        include[ancestorIndex] = true;
                    }
                }
            }

            return include;
        }

        private void AppendIncluded(IReadOnlyList<ITreeItem> source, bool[] include)
        {
            for (var index = 0; index < include.Length; index++)
            {
                if (include[index])
                {
                    this.buffer.Add(source[index]);
                }
            }
        }
    }
}
