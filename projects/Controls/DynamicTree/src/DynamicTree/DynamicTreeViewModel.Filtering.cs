// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
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

            // If filtering is being disabled (pass-all), tear down the filtered view so we don't
            // invoke predicate/builder while the control is rendering the source directly.
            if (value is null && this.filteredItems is not null)
            {
                this.filteredItems.CollectionChanged -= this.OnFilteredItemsCollectionChanged;
                this.filteredItems.Dispose();
                this.filteredItems = null;
                this.filterOptions = null;
            }

            this.filterPredicate = value;
            this.OnPropertyChanged(nameof(this.FilterPredicate));

            // Update runtime options when a filtered view exists and filtering is active.
            if (this.filterOptions is not null && this.filterPredicate is not null)
            {
                var relevantProperties = this.GetFilteringRelevantProperties();

                this.filterOptions.ObservedProperties.Clear();
                if (relevantProperties is not null)
                {
                    foreach (var p in relevantProperties)
                    {
                        this.filterOptions.ObservedProperties.Add(p);
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
            item =>
            {
                var predicate = this.filterPredicate;
                return predicate is null || predicate(item);
            },
            this.filterBuilder,
            options);

        this.filteredItems.CollectionChanged += this.OnFilteredItemsCollectionChanged;

        this.filteredItems.Refresh();

        return this.filteredItems;
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
