// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Collections;

using System.Collections.ObjectModel;
using System.Collections.Specialized;

/// <summary>
/// An observable collection, bound to source ObservableCollection, mirroring its content, but applying to it a given
/// transformation function whenever it is added or modified.
/// </summary>
/// <typeparam name="TSource">The type of elements in the source collection.</typeparam>
/// <typeparam name="TResult">The type of elements in the result collection.</typeparam>
/// <remarks>
/// This is a disposable class, and its disposal must be explicitly done when it, or the source collection, is no longer needed or
/// available.
/// </remarks>
public class DynamicObservableCollection<TSource, TResult> : ObservableCollection<TResult>, IDisposable
{
    private readonly ObservableCollection<TSource> source;
    private readonly Func<TSource, TResult> transform;

    private bool disposed;

    /// <summary>
    /// Initializes a new instance of the <see cref="DynamicObservableCollection{TSource,TResult}" /> class with the given
    /// <paramref name="source" /> collection and the given <see cref="transform">transformation function</see>.
    /// </summary>
    /// <param name="source">The source collection.</param>
    /// <param name="transform">
    /// The transformation function to be applied to each element currently in the source collection, or added in the future.
    /// </param>
    public DynamicObservableCollection(ObservableCollection<TSource> source, Func<TSource, TResult> transform)
    {
        this.source = source;
        this.transform = transform;

        // Transform and add items already in source
        foreach (var item in source)
        {
            this.Add(transform(item));
        }

        // Register for changes to the source collection
        this.source.CollectionChanged += this.OnSourceOnCollectionChanged;
    }

    /// <inheritdoc />
    /// <remarks>Call this when the collection with transformed elements is no longer needed.</remarks>
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.source.CollectionChanged -= this.OnSourceOnCollectionChanged;

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
                    this.Add(this.transform(item));
                }
            }
        }
        else if (args.Action == NotifyCollectionChangedAction.Remove)
        {
            if (args.OldItems != null)
            {
                foreach (TSource item in args.OldItems)
                {
                    var index = this.source.IndexOf(item);
                    this.RemoveAt(index);
                }
            }
        }
        else if (args.Action == NotifyCollectionChangedAction.Replace)
        {
            if (args.NewItems != null)
            {
                foreach (TSource item in args.NewItems)
                {
                    var index = this.source.IndexOf(item);
                    this[index] = this.transform(item);
                }
            }
        }
        else if (args.Action == NotifyCollectionChangedAction.Move)
        {
            var oldIndex = args.OldStartingIndex;
            var newIndex = args.NewStartingIndex;
            var item = this[oldIndex];
            this.RemoveAt(oldIndex);
            this.Insert(newIndex, item);
        }
        else if (args.Action == NotifyCollectionChangedAction.Reset)
        {
            this.Clear();

            foreach (var item in this.source)
            {
                this.Add(this.transform(item));
            }
        }
    }
}
