// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;

namespace DroidNet.Collections;

/// <summary>
/// Options that control how a <see cref="FilteredObservableCollection{T}"/> observes and rebuilds.
/// </summary>
public sealed class FilteredObservableCollectionOptions
{
    /// <summary>
    /// Gets the default options instance.
    /// </summary>
    public static FilteredObservableCollectionOptions Default { get; } = new();

    /// <summary>
    /// Gets an observable collection of property names that are observed for item property changes.
    /// <para>
    /// - An empty collection means do not observe item property changes.
    /// - A collection with entries means only those properties are considered relevant.
    /// </para>
    /// </summary>
    public ObservableCollection<string> ObservedProperties { get; } = [];

    /// <summary>
    /// Gets the debounce window for property-change driven rebuilds. Defaults to zero (immediate rebuilds).
    /// </summary>
    public TimeSpan PropertyChangedDebounceInterval { get; init; } = TimeSpan.Zero;
}
