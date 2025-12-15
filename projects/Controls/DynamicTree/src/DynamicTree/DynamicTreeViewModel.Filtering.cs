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

        view.Refresh();
    }

    /// <summary>
    ///     Returns the set of property names whose changes should cause the filtered view to refresh.
    ///     Override in concrete view models to supply application-specific properties. Returning
    ///     <see langword="null"/> means all properties are considered relevant.
    /// </summary>
    /// <returns>Property names relevant to filtering, or <see langword="null"/> to treat all as relevant.</returns>
    protected virtual IReadOnlyList<string>? GetFilteringRelevantProperties() => null;

    private FilteredObservableCollection<ITreeItem> GetOrCreateFilteredItems()
    {
        if (this.filteredItems is not null)
        {
            return this.filteredItems;
        }

        // Manual-refresh view: the view model owns all updates (hierarchical closure is global).
        this.filterBuilder ??= new HierarchicalFilterBuilder(this);

        var relevantProperties = this.GetFilteringRelevantProperties();

        var options = new FilteredObservableCollectionOptions
        {
            RelevantProperties = relevantProperties,
            ObserveSourceChanges = true,
            ObserveItemChanges = true,
            PropertyChangedDebounceInterval = TimeSpan.FromMilliseconds(FilterDebounceMilliseconds),
        };

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
