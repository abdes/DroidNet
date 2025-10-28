// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections;
using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.ComponentModel;

namespace DroidNet.Collections;

/// <summary>
///     A filtered, read-only view over an <see cref="ObservableCollection{T}"/>. Keeps itself in
///     sync with the source collection and with item property changes. The view only contains items
///     that satisfy the provided filter predicate and preserves the ordering of the source
///     collection.
/// </summary>
/// <typeparam name="T">The item type. Must implement <see cref="INotifyPropertyChanged"/>.</typeparam>
public class FilteredObservableCollection<T> : IReadOnlyList<T>, INotifyCollectionChanged, IDisposable
    where T : INotifyPropertyChanged
{
    private readonly ObservableCollection<T> source;
    private readonly Predicate<T> filter;
    private readonly HashSet<string> relevantProperties;
    private readonly List<T> filtered;

    private readonly HashSet<T> attachedItems; // Items we have subscribed to (all items from source that we've attached handlers for)
    private readonly HashSet<T> filteredSet; // Fast membership test for items currently in the filtered view
    private int suspendCount;
    private bool hadChangesWhileSuspended;
    private bool disposed;

    /// <summary>
    ///     Initializes a new instance of the <see cref="FilteredObservableCollection{T}"/> class.
    /// </summary>
    /// <param name="source">
    ///     The source <see cref="ObservableCollection{T}"/> to observe. Cannot be <see langword="null"/>.
    /// </param>
    /// <param name="filter">
    ///     A predicate that determines whether an item should appear in the filtered view. Cannot
    ///     be <see langword="null"/>.
    /// </param>
    /// <param name="relevantProperties">
    ///     Optional collection of property names that affect the filter. If omitted or empty,
    ///     changes to any property on an item are considered relevant for re-evaluating the filter
    ///     for that item. When specified, only property change notifications whose
    ///     <see cref="System.ComponentModel.PropertyChangedEventArgs.PropertyName"/> is contained
    ///     in this collection will cause the item to be re-tested against the filter.
    /// </param>
    /// <exception cref="ArgumentNullException">
    ///     Thrown when <paramref name="source"/> or <paramref name="filter"/> is <see langword="null"/>.
    /// </exception>
    public FilteredObservableCollection(
        ObservableCollection<T> source,
        Predicate<T> filter,
        IEnumerable<string>? relevantProperties = null)
    {
        this.source = source ?? throw new ArgumentNullException(nameof(source));
        this.filter = filter ?? throw new ArgumentNullException(nameof(filter));
        this.relevantProperties = relevantProperties != null
            ? new HashSet<string>(relevantProperties, StringComparer.Ordinal)
            : new HashSet<string>(StringComparer.Ordinal); // empty = all properties matter

        this.filtered = new List<T>();
        this.attachedItems = new HashSet<T>();
        this.filteredSet = new HashSet<T>();
        this.suspendCount = 0;
        this.hadChangesWhileSuspended = false;
        this.disposed = false;

        foreach (var item in this.source)
        {
            this.Attach(item);
        }

        this.source.CollectionChanged += this.OnSourceCollectionChanged;
    }

    /// <summary>
    ///     Occurs when the contents of the filtered view change.
    ///     <para>
    ///     Handlers receive <see cref="NotifyCollectionChangedEventArgs"/> describing adds,
    ///     removes, moves and resets.
    ///     </para>
    /// </summary>
    public event NotifyCollectionChangedEventHandler? CollectionChanged;

    /// <summary>
    ///     Gets the number of items in the filtered view.
    /// </summary>
    public int Count => this.filtered.Count;

    /// <summary>
    ///     Gets the element at the specified index in the filtered view.
    /// </summary>
    /// <param name="index">The zero-based index of the element to get.</param>
    /// <returns>The element at the specified index.</returns>
    /// <exception cref="ArgumentOutOfRangeException">
    ///     If <paramref name="index"/> is outside the bounds of the view.
    /// </exception>
    public T this[int index] => this.filtered[index];

    /// <summary>
    ///     Returns an enumerator that iterates through the filtered view.
    /// </summary>
    /// <returns>An enumerator for the filtered view.</returns>
    public IEnumerator<T> GetEnumerator() => this.filtered.GetEnumerator();

    /// <summary>
    ///     Returns an enumerator that iterates through the filtered view (non-generic).
    /// </summary>
    /// <returns>An enumerator for the filtered view.</returns>
    IEnumerator IEnumerable.GetEnumerator() => this.GetEnumerator();

    /// <summary>
    ///     Re-applies the filter to the entire source collection and updates the view.
    ///     Any change results in a <see cref="NotifyCollectionChangedAction.Reset"/> being raised.
    /// </summary>
    public void Refresh()
    {
        if (this.disposed)
        {
            return;
        }

        this.filtered.Clear();
        this.filteredSet.Clear();
        foreach (var item in this.source)
        {
            if (this.filter(item))
            {
                this.filtered.Add(item);
                this.filteredSet.Add(item);
            }
        }

        this.RaiseCollectionChanged(new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Reset));
    }

    /// <summary>
    ///     Temporarily suspends raising <see cref="CollectionChanged"/> events.
    /// </summary>
    /// <remarks>
    ///     Use in a using-block:
    ///     <code><![CDATA[
    ///     using(var d = view.DeferNotifications()) { /* multiple changes */ }
    ///     ]]></code>
    ///     <para>
    ///     When the returned <see cref="IDisposable"/> is disposed the events are resumed and a
    ///     <see cref="NotifyCollectionChangedAction.Reset"/> will be raised if any changes occurred
    ///     while suspended.
    ///     </para>
    /// </remarks>
    /// <returns>An <see cref="IDisposable"/> which when disposed resumes notifications.</returns>
    /// <exception cref="ObjectDisposedException">If the view has already been disposed.</exception>
    public IDisposable DeferNotifications()
    {
        ObjectDisposedException.ThrowIf(this.disposed, nameof(FilteredObservableCollection<>));

        this.suspendCount++;
        return new NotificationSuspender(this);
    }

    /// <summary>
    ///     Releases the resources used by the <see cref="FilteredObservableCollection{T}"/>. This
    ///     unsubscribes from the source collection and from item property changed events and clears
    ///     the view.
    /// </summary>
    public void Dispose()
    {
        this.Dispose(true);
        GC.SuppressFinalize(this);
    }

    /// <summary>
    ///     Releases the unmanaged resources used by the <see cref="FilteredObservableCollection{T}"/>
    ///     and optionally releases the managed resources.
    /// </summary>
    /// <param name="disposing">
    ///     If <see langword="true"/>, release both managed and unmanaged resources.
    ///     If <see langword="false"/>, release only unmanaged resources.
    /// </param>
    protected virtual void Dispose(bool disposing)
    {
        if (this.disposed)
        {
            return;
        }

        if (disposing)
        {
            this.source.CollectionChanged -= this.OnSourceCollectionChanged;
            foreach (var item in this.attachedItems.ToList())
            {
                item.PropertyChanged -= this.OnItemPropertyChanged;
            }

            this.attachedItems.Clear();
            this.filteredSet.Clear();
            this.filtered.Clear();

            // Drop subscribers to our event
            this.CollectionChanged = null;
        }

        this.disposed = true;
    }

    private void OnSourceCollectionChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        if (this.disposed)
        {
            return;
        }

        switch (e.Action)
        {
            case NotifyCollectionChangedAction.Add:
                this.HandleAdd(e);
                break;

            case NotifyCollectionChangedAction.Remove:
                this.HandleRemove(e);
                break;

            case NotifyCollectionChangedAction.Move:
                this.HandleMove(e);
                break;

            case NotifyCollectionChangedAction.Replace:
                this.HandleReplace(e);
                break;

            case NotifyCollectionChangedAction.Reset:
            default:
                this.HandleReset();
                break;
        }
    }

    private void HandleAdd(NotifyCollectionChangedEventArgs e)
    {
        if (e.NewItems == null)
        {
            return;
        }

        foreach (T item in e.NewItems)
        {
            // Attach (subscribe) and let Attach add to filtered if it matches.
            this.Attach(item);

            if (this.filteredSet.Contains(item))
            {
                var index = this.filtered.IndexOf(item);
                this.RaiseCollectionChanged(new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Add, item, index));
            }
        }
    }

    private void HandleRemove(NotifyCollectionChangedEventArgs e)
    {
        if (e.OldItems == null)
        {
            return;
        }

        foreach (T item in e.OldItems)
        {
            var index = this.filtered.IndexOf(item);
            this.Detach(item);
            if (index >= 0)
            {
                this.RaiseCollectionChanged(new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Remove, item, index));
            }
        }
    }

    private void HandleMove(NotifyCollectionChangedEventArgs e)
    {
        if (e.OldItems == null)
        {
            return;
        }

        foreach (T item in e.OldItems)
        {
            var oldIndex = this.filtered.IndexOf(item);
            if (oldIndex >= 0)
            {
                // remove from current position
                this.filtered.RemoveAt(oldIndex);
                _ = this.filteredSet.Remove(item);

                // compute new index according to the new source order
                var newIndex = this.ComputeInsertIndex(item);
                this.filtered.Insert(newIndex, item);
                this.filteredSet.Add(item);

                this.RaiseCollectionChanged(new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Move, item, newIndex, oldIndex));
            }
        }
    }

    private void HandleReplace(NotifyCollectionChangedEventArgs e)
    {
        if (e.OldItems != null)
        {
            foreach (T oldItem in e.OldItems)
            {
                var index = this.filtered.IndexOf(oldItem);
                this.Detach(oldItem);
                if (index >= 0)
                {
                    this.RaiseCollectionChanged(new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Remove, oldItem, index));
                }
            }
        }

        if (e.NewItems != null)
        {
            foreach (T newItem in e.NewItems)
            {
                this.Attach(newItem);
                if (this.filteredSet.Contains(newItem))
                {
                    var index = this.filtered.IndexOf(newItem);
                    this.RaiseCollectionChanged(new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Add, newItem, index));
                }
            }
        }
    }

    private void HandleReset()
    {
        // Rebuild all subscriptions and the view
        this.RebuildAllFromSource();
    }

    private void Attach(T item)
    {
        // Only subscribe once per item
        if (this.attachedItems.Add(item))
        {
            item.PropertyChanged += this.OnItemPropertyChanged;
        }

        if (this.filter(item))
        {
            var index = this.ComputeInsertIndex(item);
            this.filtered.Insert(index, item);
            this.filteredSet.Add(item);
        }
    }

    private void Detach(T item)
    {
        if (this.attachedItems.Remove(item))
        {
            item.PropertyChanged -= this.OnItemPropertyChanged;
        }

        var removed = this.filtered.Remove(item);
        if (removed)
        {
            this.filteredSet.Remove(item);
        }
    }

    private void OnItemPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (this.disposed)
        {
            return;
        }

        if (sender is not T item)
        {
            return;
        }

        if (this.relevantProperties.Count == 0 || this.relevantProperties.Contains(e.PropertyName!))
        {
            var inFiltered = this.filteredSet.Contains(item);
            var shouldBeIn = this.filter(item);

            if (inFiltered && !shouldBeIn)
            {
                var index = this.filtered.IndexOf(item);
                if (index >= 0)
                {
                    this.filtered.RemoveAt(index);
                    this.filteredSet.Remove(item);
                    this.RaiseCollectionChanged(new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Remove, item, index));
                }
            }
            else if (!inFiltered && shouldBeIn)
            {
                var index = this.ComputeInsertIndex(item);
                this.filtered.Insert(index, item);
                this.filteredSet.Add(item);
                this.RaiseCollectionChanged(new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Add, item, index));
            }
        }
    }

    private int ComputeInsertIndex(T item)
    {
        // Maintain same ordering as source.
        // Optimized: iterate the source up to the item's position and count how many of those
        // are currently present in the filtered view. This avoids repeated IndexOf calls.
        var count = 0;
        var comparer = EqualityComparer<T>.Default;

        for (var i = 0; i < this.source.Count; i++)
        {
            var s = this.source[i];
            if (comparer.Equals(s, item))
            {
                break;
            }

            if (this.filteredSet.Contains(s))
            {
                count++;
            }
        }

        return count;
    }

    private void ResumeNotifications()
    {
        if (this.suspendCount <= 0)
        {
            return;
        }

        this.suspendCount--;
        if (this.suspendCount == 0 && this.hadChangesWhileSuspended)
        {
            this.hadChangesWhileSuspended = false;
            this.RaiseCollectionChanged(new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Reset));
        }
    }

    private void RaiseCollectionChanged(NotifyCollectionChangedEventArgs args)
    {
        if (this.suspendCount > 0)
        {
            this.hadChangesWhileSuspended = true;
            return;
        }

        this.CollectionChanged?.Invoke(this, args);
    }

    /// <summary>
    /// Rebuilds all subscriptions and the filtered view from the current source contents
    /// and raises Reset. Used for Reset actions on the source collection.
    /// </summary>
    private void RebuildAllFromSource()
    {
        // Unsubscribe from all previously attached items
        foreach (var item in this.attachedItems.ToList())
        {
            item.PropertyChanged -= this.OnItemPropertyChanged;
        }

        this.attachedItems.Clear();
        this.filtered.Clear();
        this.filteredSet.Clear();

        foreach (var item in this.source)
        {
            this.Attach(item);
        }

        this.RaiseCollectionChanged(new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Reset));
    }

    private readonly struct NotificationSuspender(FilteredObservableCollection<T> owner) : IDisposable
    {
        private readonly FilteredObservableCollection<T> owner = owner;

        public void Dispose() => this.owner.ResumeNotifications();
    }
}
