// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Collections;

using System.Collections.ObjectModel;
using System.Collections.Specialized;

/// <summary>
/// An observable collection, bound to source ObservableCollection, mirroring its content, but
/// applying to it a given transformation function whenever it is added or modified.
/// </summary>
/// <typeparam name="TSource">The type of elements in the source collection.</typeparam>
/// <typeparam name="TResult">The type of elements in the result collection.</typeparam>
/// <remarks>
/// This is a disposable class, and its disposal must be explicitly done when it, or the source
/// collection, is no longer needed or available.
/// </remarks>
public class DynamicObservableCollection<TSource, TResult> : ObservableCollection<TResult>, IDisposable
{
    private readonly ObservableCollection<TSource> source;
    private readonly Func<TSource, TResult> transform;

    private bool disposed;

    /// <summary>
    /// Initializes a new instance of the <see cref="DynamicObservableCollection{TSource,TResult}" />
    /// class with the given <paramref name="source" /> collection and the given <see cref="transform" />
    /// function.
    /// </summary>
    /// <param name="source">The source collection.</param>
    /// <param name="transform">
    /// The transformation function to be applied to each element currently in the source
    /// collection, or added in the future.
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
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    /// <summary>
    /// Releases the unmanaged resources used by the <see cref="DynamicObservableCollection{TSource,TResult}" />
    /// and optionally releases the managed resources.
    /// </summary>
    /// <param name="disposing">
    /// <see langword="true" /> to release both managed and unmanaged resources; <see langword="false" />
    /// to release only unmanaged resources.
    /// </param>
    protected virtual void Dispose(bool disposing)
    {
        if (this.disposed)
        {
            return;
        }

        if (disposing)
        {
            // Dispose managed resources
            this.source.CollectionChanged -= this.OnSourceOnCollectionChanged;
        }

        this.disposed = true;
    }

    private void OnSourceOnCollectionChanged(object? sender, NotifyCollectionChangedEventArgs args)
    {
        switch (args.Action)
        {
            case NotifyCollectionChangedAction.Add:
                this.HandleItemsAdded(args);
                break;
            case NotifyCollectionChangedAction.Remove:
                this.HandleItemsRemoved(args);
                break;
            case NotifyCollectionChangedAction.Replace:
                this.HandleItemsReplaced(args);
                break;
            case NotifyCollectionChangedAction.Move:
                this.HandleItemMoved(args);
                break;
            case NotifyCollectionChangedAction.Reset:
                this.HandleCollectionCleared();
                break;
            default:
                throw new ArgumentOutOfRangeException(nameof(args), $"unexpected `{args.Action}`");
        }
    }

    private void HandleCollectionCleared()
    {
        this.Clear();

        foreach (var item in this.source)
        {
            this.Add(this.transform(item));
        }
    }

    private void HandleItemMoved(NotifyCollectionChangedEventArgs args)
    {
        var oldIndex = args.OldStartingIndex;
        var newIndex = args.NewStartingIndex;
        var item = this[oldIndex];
        this.RemoveAt(oldIndex);
        this.Insert(newIndex, item);
    }

    private void HandleItemsReplaced(NotifyCollectionChangedEventArgs args)
    {
        if (args.NewItems == null)
        {
            return;
        }

        var index = args.NewStartingIndex;
        foreach (TSource item in args.NewItems)
        {
            this[index++] = this.transform(item);
        }
    }

    private void HandleItemsRemoved(NotifyCollectionChangedEventArgs args)
    {
        if (args.OldItems == null)
        {
            return;
        }

        for (var itemNumber = 0; itemNumber < args.OldItems.Count; itemNumber++)
        {
            this.RemoveAt(args.OldStartingIndex);
        }
    }

    private void HandleItemsAdded(NotifyCollectionChangedEventArgs args)
    {
        if (args.NewItems == null)
        {
            return;
        }

        var index = args.NewStartingIndex;
        foreach (TSource item in args.NewItems)
        {
            this.Insert(index++, this.transform(item));
        }
    }
}
