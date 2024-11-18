// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.Diagnostics.CodeAnalysis;

namespace DroidNet.Collections;

/// <summary>
/// An observable collection that mirrors a source <see cref="ObservableCollection{TSource}"/> and
/// applies a transformation function to each element.
/// </summary>
/// <typeparam name="TSource">The type of elements in the source collection.</typeparam>
/// <typeparam name="TResult">The type of elements in the result collection.</typeparam>
/// <remarks>
/// This class is disposable, and its disposal must be explicitly done when it, or the source
/// collection, is no longer needed or available.
/// </remarks>
/// <example>
/// <para><strong>Example Usage</strong></para>
/// <code><![CDATA[
/// var source = new ObservableCollection<int> { 1, 2, 3 };
/// var result = new DynamicObservableCollection<int, string>(source, x => $"Item {x * 10}");
///
/// // Now, any changes to 'source' will be reflected in 'result'
/// source.Add(4);
/// Console.WriteLine(string.Join(", ", result)); // Output: "Item 10, Item 20, Item 30, Item 40"
///
/// // Don't forget to dispose when no longer needed
/// result.Dispose();
/// ]]></code>
/// </example>
[SuppressMessage("ReSharper", "ClassWithVirtualMembersNeverInherited.Global", Justification = "class maybe extended externally")]
public class DynamicObservableCollection<TSource, TResult> : ObservableCollection<TResult>, IDisposable
{
    private readonly ObservableCollection<TSource> source;
    private readonly Func<TSource, TResult> transform;
    private bool disposed;

    /// <summary>
    /// Initializes a new instance of the <see cref="DynamicObservableCollection{TSource,TResult}"/>
    /// class with the given source collection and transformation function.
    /// </summary>
    /// <param name="source">The source collection.</param>
    /// <param name="transform">The transformation function to be applied to each element currently in the source collection, or added in the future.</param>
    /// <exception cref="ArgumentNullException">Thrown if <paramref name="source"/> or <paramref name="transform"/> is <see langword="null"/>.</exception>
    public DynamicObservableCollection(ObservableCollection<TSource> source, Func<TSource, TResult> transform)
    {
        this.source = source ?? throw new ArgumentNullException(nameof(source));
        this.transform = transform ?? throw new ArgumentNullException(nameof(transform));

        // Transform and add items already in source
        foreach (var item in source)
        {
            this.Add(transform(item));
        }

        // Register for changes to the source collection
        this.source.CollectionChanged += this.OnSourceOnCollectionChanged;
    }

    /// <inheritdoc />
    /// <remarks>
    /// Call this method when the collection with transformed elements is no longer needed.
    /// </remarks>
    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    /// <summary>
    /// Releases the unmanaged resources used by the <see cref="DynamicObservableCollection{TSource,TResult}"/> and optionally releases the managed resources.
    /// </summary>
    /// <param name="disposing">
    /// <see langword="true"/> to release both managed and unmanaged resources; <see langword="false"/> to release only unmanaged resources.
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

    /// <summary>
    /// Handles changes in the source collection and updates the transformed collection accordingly.
    /// </summary>
    /// <param name="sender">The source collection.</param>
    /// <param name="args">The event data.</param>
    /// <exception cref="ArgumentOutOfRangeException">Thrown if an unexpected <see cref="NotifyCollectionChangedAction"/> is encountered.</exception>
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
                throw new ArgumentOutOfRangeException(nameof(args), $"Unexpected `{args.Action}`");
        }
    }

    /// <summary>
    /// Handles the addition of items to the source collection.
    /// </summary>
    /// <param name="args">The event data.</param>
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

    /// <summary>
    /// Handles the removal of items from the source collection.
    /// </summary>
    /// <param name="args">The event data.</param>
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

    /// <summary>
    /// Handles the replacement of items in the source collection.
    /// </summary>
    /// <param name="args">The event data.</param>
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

    /// <summary>
    /// Handles the movement of items within the source collection.
    /// </summary>
    /// <param name="args">The event data.</param>
    private void HandleItemMoved(NotifyCollectionChangedEventArgs args)
    {
        var oldIndex = args.OldStartingIndex;
        var newIndex = args.NewStartingIndex;
        var item = this[oldIndex];
        this.RemoveAt(oldIndex);
        this.Insert(newIndex, item);
    }

    /// <summary>
    /// Handles the clearing of the source collection.
    /// </summary>
    private void HandleCollectionCleared()
    {
        this.Clear();

        foreach (var item in this.source)
        {
            this.Add(this.transform(item));
        }
    }
}
