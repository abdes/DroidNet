// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;

namespace DroidNet.Collections;

/// <summary>
/// Provides extension methods for <see cref="ObservableCollection{T}"/> to enhance its functionality.
/// </summary>
public static class ObservableCollectionExtensions
{
    /// <summary>
    /// Inserts an item into a sorted <see cref="ObservableCollection{T}"/> in the correct place,
    /// based on the provided key getter and key comparer.
    /// </summary>
    /// <typeparam name="TItem">The type of elements in the collection.</typeparam>
    /// <typeparam name="TOrderBy">The type of key by which the collection is ordered.</typeparam>
    /// <param name="collection">The collection being modified.</param>
    /// <param name="itemToAdd">
    /// The object to be inserted in the collection. The value can be <see langword="null"/> for reference types.
    /// </param>
    /// <param name="keyGetter">The function to extract the key used to compare items.</param>
    /// <param name="comparer">
    /// The comparison object used to compare two keys. Defaults to <see cref="Comparer{T}.Default"/>.
    /// </param>
    /// <example>
    /// <para><strong>Example Usage</strong></para>
    /// <code><![CDATA[
    /// var collection = new ObservableCollection<int> { 1, 3, 5, 7 };
    /// const int itemToAdd = 4;
    /// collection.InsertInPlace(itemToAdd, x => x, Comparer<int>.Default);
    /// // collection is now { 1, 3, 4, 5, 7 }
    /// ]]></code>
    /// </example>
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

    /// <summary>
    /// An extension method for <see cref="ObservableCollection{T}"/> which creates a new
    /// <see cref="DynamicObservableCollection{TSource,TResult}"/> containing the elements of the
    /// <paramref name="source"/> collection, to which the <paramref name="transform"/> function
    /// has been applied. Additionally, any future modification to the <paramref name="source"/>
    /// collection will be reflected as well.
    /// </summary>
    /// <typeparam name="TSource">The type of elements in the source collection.</typeparam>
    /// <typeparam name="TResult">The type of elements in the result collection.</typeparam>
    /// <param name="source">The source collection.</param>
    /// <param name="transform">
    /// The transformation function to be applied to each element currently in the source
    /// collection, or added in the future.
    /// </param>
    /// <returns>
    /// A <see cref="DynamicObservableCollection{TSource,TResult}"/> instance. Call
    /// <see cref="DynamicObservableCollection{TSource,TResult}.Dispose()"/> on it when it is no
    /// longer needed to unsubscribe from the source collection change notifications.
    /// </returns>
    /// <example>
    /// <para><strong>Example Usage</strong></para>
    /// <code><![CDATA[
    /// var source = new ObservableCollection<int> { 1, 2, 3 };
    /// var result = source.Transform(x => x.ToString());
    /// source.Add(4); // result is now { "1", "2", "3", "4" }
    /// result.Dispose();
    /// ]]></code>
    /// </example>
    public static DynamicObservableCollection<TSource, TResult> Transform<TSource, TResult>(
        this ObservableCollection<TSource> source,
        Func<TSource, TResult> transform)
        => new(source, transform);

    /// <summary>
    /// Performs a binary search on a list to find the index of the specified key.
    /// </summary>
    /// <typeparam name="TItem">The type of elements in the list.</typeparam>
    /// <typeparam name="TOrderBy">The type of key by which the list is ordered.</typeparam>
    /// <param name="collection">The list being searched.</param>
    /// <param name="keyToFind">The key to find in the list.</param>
    /// <param name="comparer">The comparison object used to compare two keys.</param>
    /// <param name="keyGetter">The function to extract the key used to compare items.</param>
    /// <returns>The index of the specified key in the list.</returns>
    /// <exception cref="ArgumentNullException">Thrown if <paramref name="collection"/> is <see langword="null"/>.</exception>
    private static int BinarySearch<TItem, TOrderBy>(
        this List<TItem> collection,
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
