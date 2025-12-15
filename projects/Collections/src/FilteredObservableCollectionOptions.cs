// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

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
    /// Gets the property names that are relevant to filtering. <see langword="null"/> or empty means all properties.
    /// </summary>
    public IEnumerable<string>? RelevantProperties { get; init; }

    /// <summary>
    /// Gets a value indicating whether the view observes source collection changes. Defaults to <see langword="true"/>.
    /// </summary>
    public bool ObserveSourceChanges { get; init; } = true;

    /// <summary>
    /// Gets a value indicating whether the view observes item property changes. Defaults to <see langword="true"/>.
    /// </summary>
    public bool ObserveItemChanges { get; init; } = true;

    /// <summary>
    /// Gets the debounce window for property-change driven rebuilds. Defaults to zero (immediate rebuilds).
    /// </summary>
    public TimeSpan PropertyChangedDebounceInterval { get; init; } = TimeSpan.Zero;
}
