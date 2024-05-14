// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Collections;

using System.Collections.ObjectModel;
using System.Collections.Specialized;

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

    /// <summary>
    /// An extension method for <see cref="ObservableCollection{T}" /> which creates a <see cref="Transformer{TSource,TResult}" />
    /// that can transform items in this collection using the provided function, to create a new collection of items with type
    /// <typeparamref name="TResult" />.
    /// </summary>
    /// <typeparam name="TSource">The type of elements in the source collection.</typeparam>
    /// <typeparam name="TResult">The type of elements in the result collection.</typeparam>
    /// <param name="source">The source collection.</param>
    /// <param name="transform">
    /// The transformation function to be applied to each element currently in the source collection, or added in the future.
    /// </param>
    /// <returns>
    /// A <see cref="Transformer{TSource,TResult}" /> instance. Call <see cref="Transformer{TSource,TResult}.Transform" /> on it
    /// to get the transformed collection.
    /// </returns>
    /// <seealso cref="Transformer{TSource,TResult}" />
    public static Transformer<TSource, TResult> WithTransformer<TSource, TResult>(
        this ObservableCollection<TSource> source,
        Func<TSource, TResult> transform) => new(source, transform);

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

    /// <summary>
    /// A transformer that can transform a source collection into a new result collection, where each element in the result is
    /// obtained by applying the transformation function to the corresonding element in the source. The transformation only
    /// happens when the <see cref="Transform" /> method is called.
    /// </summary>
    /// <typeparam name="TSource">The type of elements in the source collection.</typeparam>
    /// <typeparam name="TResult">The type of elements in the result collection.</typeparam>
    /// <param name="source">The source collection.</param>
    /// <param name="transform">
    /// The transformation function to apply to each element currently in the source collection, or added in the future.
    /// </param>
    /// <example>
    /// Here's an example usage:
    /// <code>
    /// <![CDATA[
    /// var source = new ObservableCollection<int> { 1, 2, 3 };
    /// var (result, subscription) = source.WithTransformer(x => x.ToString()).Transform();
    /// source.Add(4); // result is now { "1", "2", "3", "4" }
    ///
    /// subscription.Dispose();
    /// ]]>
    /// </code>
    /// </example>
    public class Transformer<TSource, TResult>(ObservableCollection<TSource> source, Func<TSource, TResult> transform)
        : IDisposable
    {
        private bool disposed;

        private ObservableCollection<TResult>? result;

        /// <summary>
        /// Apply the transformation on the source collection if it has not already been applied, and get the result.
        /// </summary>
        /// <returns>
        /// The new collection with elements (current and future) from the source collection transformed using the transformation
        /// function provided when this Transformer was created, and a <see cref="IDisposable">disposable</see> subscription that
        /// needs to be disposed of when the new collection is no longer needed.
        /// </returns>
        public (ObservableCollection<TResult> result, IDisposable subscription) Transform()
        {
            if (this.result is not null)
            {
                return (this.result, this);
            }

            // Transform and add items already in source
            this.result = [];
            foreach (var item in source)
            {
                this.result.Add(transform(item));
            }

            // Register for changes to the source collection
            source.CollectionChanged += this.OnSourceOnCollectionChanged;

            return (this.result, this);
        }

        /// <inheritdoc />
        /// <remarks>Call this when the collection with transformed elements is no longer needed.</remarks>
        public void Dispose()
        {
            if (this.disposed)
            {
                return;
            }

            source.CollectionChanged -= this.OnSourceOnCollectionChanged;

            this.disposed = true;
            GC.SuppressFinalize(this);
        }

        private void OnSourceOnCollectionChanged(object? sender, NotifyCollectionChangedEventArgs args)
        {
            if (args.Action == NotifyCollectionChangedAction.Add)
            {
                if (args.NewItems != null)
                {
                    foreach (TSource item in args.NewItems)
                    {
                        this.result!.Add(transform(item));
                    }
                }
            }
            else if (args.Action == NotifyCollectionChangedAction.Remove)
            {
                if (args.OldItems != null)
                {
                    foreach (TSource item in args.OldItems)
                    {
                        var index = source.IndexOf(item);
                        this.result!.RemoveAt(index);
                    }
                }
            }
            else if (args.Action == NotifyCollectionChangedAction.Replace)
            {
                if (args.NewItems != null)
                {
                    foreach (TSource item in args.NewItems)
                    {
                        var index = source.IndexOf(item);
                        this.result![index] = transform(item);
                    }
                }
            }
            else if (args.Action == NotifyCollectionChangedAction.Move)
            {
                var oldIndex = args.OldStartingIndex;
                var newIndex = args.NewStartingIndex;
                var item = this.result![oldIndex];
                this.result.RemoveAt(oldIndex);
                this.result.Insert(newIndex, item);
            }
            else if (args.Action == NotifyCollectionChangedAction.Reset)
            {
                this.result!.Clear();

                foreach (var item in source)
                {
                    this.result.Add(transform(item));
                }
            }
        }
    }
}
