// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Collections;

using System.Collections.ObjectModel;

public static class ObservableCollectionExtensions
{
    /// <summary>
    /// Inserts an item into a sorted <see cref="ObservableCollection{T}" /> in the correct place, based on the provided key getter
    /// and key comparer.
    /// </summary>
    /// <typeparam name="TItem">The type of elements in the collection.</typeparam>
    /// <typeparam name="TOrderBy">The type of key by which the collection is ordered.</typeparam>
    /// <param name="collection">The collection being modified.</param>
    /// <param name="itemToAdd">The object to be inserted in the collection. The value can be null for reference types.</param>
    /// <param name="keyGetter">The function to extract the key used to compare items.</param>
    /// <param name="comparer">The comparison object used to compare two keys. Defaults to <see cref="Comparer{T}.Default" />.</param>
    public static void InsertInPlace<TItem, TOrderBy>(
        this ObservableCollection<TItem> collection,
        TItem itemToAdd,
        Func<TItem, TOrderBy> keyGetter,
        Comparer<TOrderBy>? comparer = null)
    {
        comparer ??= Comparer<TOrderBy>.Default;
        var index = collection.ToList()
            .BinarySearch(keyGetter(itemToAdd), comparer, keyGetter);
        collection.Insert(index, itemToAdd);
    }

    private static int BinarySearch<TItem, TOrderBy>(
        this IList<TItem> collection,
        TOrderBy keyToFind,
        Comparer<TOrderBy> comparer,
        Func<TItem, TOrderBy> keyGetter)
    {
        ArgumentNullException.ThrowIfNull(collection);

        var lower = 0;
        var upper = collection.Count - 1;

        while (lower <= upper)
        {
            var middle = lower + ((upper - lower) / 2);
            switch (comparer.Compare(keyToFind, keyGetter.Invoke(collection[middle])))
            {
                case 0:
                    return middle + 1;
                case < 0:
                    upper = middle - 1;
                    break;
                default:
                    lower = middle + 1;
                    break;
            }
        }

        // If we cannot find the item, return the item below it, so the new
        // item will be inserted next.
        return lower;
    }
}
