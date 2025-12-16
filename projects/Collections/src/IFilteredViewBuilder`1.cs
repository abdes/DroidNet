// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Collections;

/// <summary>
///     Defines a builder that can materialize a filtered projection from a source collection in a
///     single operation. This is useful when filtering depends on global state (for example,
///     ancestor/descendant relationships) and cannot be represented by a stateless predicate.
/// </summary>
/// <typeparam name="T">The item type.</typeparam>
public interface IFilteredViewBuilder<T>
{
    /// <summary>
    /// Computes the set of consequential changes (inclusions or exclusions) resulting from a single item's
    /// inclusion status change.
    /// </summary>
    /// <param name="changedItem">The item that triggered the change.</param>
    /// <param name="becameIncluded">
    /// <see langword="true"/> if <paramref name="changedItem"/> is now included by the primary filter;
    /// <see langword="false"/> if it is now excluded.
    /// </param>
    /// <param name="source">The full source collection, used for context if needed.</param>
    /// <returns>
    /// A set of items to include (if <paramref name="becameIncluded"/> is true) or exclude (if false).
    /// The set must contain exact reference-equal instances from <paramref name="source"/> and no duplicates.
    /// </returns>
    public IReadOnlySet<T> BuildForChangedItem(T changedItem, bool becameIncluded, IReadOnlyList<T> source);
}
