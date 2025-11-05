// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;

namespace DroidNet.Routing.Utils;

/// <summary>
/// Contains extension methods for the <see cref="ReadOnlyCollection{T}" /> type.
/// </summary>
public static class ReadOnlyCollectionExtensions
{
    /// <summary>
    /// Creates a shallow copy of a range of elements in the source <see cref="ReadOnlyCollection{T}" />.
    /// </summary>
    /// <typeparam name="T">The type of elements in the collection.</typeparam>
    /// <param name="collection">The source <see cref="ReadOnlyCollection{T}" />.</param>
    /// <param name="start">The zero-based index at which the range starts.</param>
    /// <param name="count">The number of elements in the range.</param>
    /// <returns>A shallow copy of a range of elements in the source <see cref="ReadOnlyCollection{T}" />.</returns>
    /// <exception cref="ArgumentOutOfRangeException">
    /// Thrown when index and count do not denote a valid range of elements in the <see cref="ReadOnlyCollection{T}" />.
    /// </exception>
    public static IReadOnlyList<T> GetRange<T>(this IReadOnlyList<T> collection, int start, int count)
    {
        ArgumentOutOfRangeException.ThrowIfLessThan(start, 0);
        ArgumentOutOfRangeException.ThrowIfLessThan(count, 0);
        ArgumentOutOfRangeException.ThrowIfGreaterThan(count, collection.Count - start);

        var subset = new List<T>();
        for (var i = start; i < start + count && i < collection.Count; i++)
        {
            subset.Add(collection[i]);
        }

        return subset;
    }
}
