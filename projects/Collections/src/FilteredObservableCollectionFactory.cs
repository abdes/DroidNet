// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;

namespace DroidNet.Collections;

/// <summary>
/// Factory helpers for creating <see cref="FilteredObservableCollection{T}"/> instances.
/// </summary>
public static class FilteredObservableCollectionFactory
{
    /// <summary>
    /// Creates a builder-based filtered view.
    /// </summary>
    /// <typeparam name="T">The item type.</typeparam>
    /// <param name="source">The source collection to observe.</param>
    /// <param name="viewBuilder">Builder that materializes the filtered projection.</param>
    /// <param name="options">Optional settings controlling relevant properties, observation, and debounce.</param>
    /// <returns>A new filtered view over <paramref name="source"/>.</returns>
    public static FilteredObservableCollection<T> FromBuilder<T>(
        ObservableCollection<T> source,
        IFilteredViewBuilder<T> viewBuilder,
        FilteredObservableCollectionOptions? options = null)
        where T : class
    {
        ArgumentNullException.ThrowIfNull(viewBuilder);
        return new FilteredObservableCollection<T>(source, viewBuilder, options ?? FilteredObservableCollectionOptions.Default);
    }

    /// <summary>
    /// Creates a predicate-based filtered view.
    /// </summary>
    /// <typeparam name="T">The item type.</typeparam>
    /// <param name="source">The source collection to observe.</param>
    /// <param name="filter">Predicate that determines inclusion in the view.</param>
    /// <param name="options">Optional settings controlling relevant properties and observation behavior.</param>
    /// <returns>A new filtered view over <paramref name="source"/>.</returns>
    public static FilteredObservableCollection<T> FromPredicate<T>(
        ObservableCollection<T> source,
        Predicate<T> filter,
        FilteredObservableCollectionOptions? options = null)
        where T : class
    {
        ArgumentNullException.ThrowIfNull(filter);
        return new FilteredObservableCollection<T>(source, filter, options ?? FilteredObservableCollectionOptions.Default);
    }
}
