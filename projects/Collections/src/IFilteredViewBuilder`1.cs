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
    ///     Builds the filtered view for the provided <paramref name="source"/> collection.
    /// </summary>
    /// <param name="source">The source items to evaluate.</param>
    /// <returns>A filtered projection preserving the order of <paramref name="source"/>.</returns>
    public IReadOnlyList<T> Build(IReadOnlyList<T> source);
}
