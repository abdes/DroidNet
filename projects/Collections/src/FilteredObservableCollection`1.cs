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
/// <typeparam name="T">The item type.</typeparam>
public sealed class FilteredObservableCollection<T> : IReadOnlyList<T>, IList<T>, IList, INotifyCollectionChanged, IDisposable
    where T : class
{
    private readonly ObservableCollection<T> source;
    private readonly Predicate<T>? filter;
    private readonly IFilteredViewBuilder<T>? viewBuilder;
    private readonly bool useBuilder;
    private readonly HashSet<string> relevantProperties;

    private readonly List<T> filtered;
    private readonly bool debounceEnabled;
    private readonly TimeSpan propertyChangedDebounceInterval;
    private readonly SynchronizationContext? synchronizationContext;
    private readonly Lock debounceGate = new();

    private readonly FilteredObservableCollectionOptions options;

    private readonly HashSet<T> attachedItems; // Items we have subscribed to (all items from source that we've attached handlers for)
    private readonly HashSet<T> propertySubscribedItems; // Items we have attached PropertyChanged handler for
    private readonly HashSet<T> filteredSet; // Fast membership test for items currently in the filtered view
    private readonly HashSet<T> dirtyItems = []; // Items that changed during debounce window (predicate mode)
    private int suspendCount;
    private bool hadChangesWhileSuspended;
    private Timer? debounceTimer;
    private bool debouncePending;
    private bool disposed;

    /// <summary>
    /// Initializes a new instance of the <see cref="FilteredObservableCollection{T}"/> class using a predicate filter.
    /// </summary>
    /// <param name="source">The source <see cref="ObservableCollection{T}"/> to observe.</param>
    /// <param name="filter">Predicate that determines whether an item should appear in the filtered view.</param>
    /// <param name="options">Optional settings controlling relevant properties and observation behavior.</param>
    /// <exception cref="ArgumentNullException">Thrown when <paramref name="source"/> or <paramref name="filter"/> is <see langword="null"/>.</exception>
    internal FilteredObservableCollection(
        ObservableCollection<T> source,
        Predicate<T> filter,
        FilteredObservableCollectionOptions? options = null)
    {
        this.source = source ?? throw new ArgumentNullException(nameof(source));
        this.filter = filter ?? throw new ArgumentNullException(nameof(filter));

        var resolvedOptions = options ?? FilteredObservableCollectionOptions.Default;
        this.options = resolvedOptions;

        this.relevantProperties = new HashSet<string>(resolvedOptions.ObservedProperties, StringComparer.Ordinal);

        this.propertyChangedDebounceInterval = resolvedOptions.PropertyChangedDebounceInterval;
        this.debounceEnabled = this.propertyChangedDebounceInterval > TimeSpan.Zero;
        this.synchronizationContext = SynchronizationContext.Current;

        // Always observe source collection changes — the filtered view must stay in sync.
        this.source.CollectionChanged += this.OnSourceCollectionChanged;

        // Listen for mutations of the ObservedProperties collection so the view can react to changes.
        resolvedOptions.ObservedProperties.CollectionChanged += this.OnObservedPropertiesCollectionChanged;

        this.filtered = [];
        this.attachedItems = [];
        this.propertySubscribedItems = [];
        this.filteredSet = [];
        this.suspendCount = 0;
        this.hadChangesWhileSuspended = false;
        this.disposed = false;

        foreach (var item in this.source)
        {
            this.Attach(item);
        }
    }

    /// <summary>
    /// Initializes a new instance of the <see cref="FilteredObservableCollection{T}"/> class using a builder.
    /// </summary>
    /// <param name="source">The source collection to observe.</param>
    /// <param name="viewBuilder">Builder that materializes the filtered projection.</param>
    /// <param name="options">Settings controlling relevant properties, observation, and debounce.</param>
    internal FilteredObservableCollection(
        ObservableCollection<T> source,
        IFilteredViewBuilder<T> viewBuilder,
        FilteredObservableCollectionOptions options)
    {
        this.source = source ?? throw new ArgumentNullException(nameof(source));
        this.viewBuilder = viewBuilder ?? throw new ArgumentNullException(nameof(viewBuilder));
        this.filter = null;
        this.useBuilder = true;

        var resolvedOptions = options ?? FilteredObservableCollectionOptions.Default;
        this.options = resolvedOptions;

        this.relevantProperties = new HashSet<string>(resolvedOptions.ObservedProperties, StringComparer.Ordinal);

        this.propertyChangedDebounceInterval = resolvedOptions.PropertyChangedDebounceInterval;
        this.debounceEnabled = this.propertyChangedDebounceInterval > TimeSpan.Zero;
        this.synchronizationContext = SynchronizationContext.Current;

        this.source.CollectionChanged += this.OnSourceCollectionChanged;

        resolvedOptions.ObservedProperties.CollectionChanged += this.OnObservedPropertiesCollectionChanged;

        this.filtered = [];
        this.attachedItems = [];
        this.propertySubscribedItems = [];
        this.filteredSet = [];
        this.suspendCount = 0;
        this.hadChangesWhileSuspended = false;
        this.disposed = false;

        foreach (var item in this.source)
        {
            this.Attach(item);
        }

        this.RebuildWithBuilder(resetOnly: false);
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
    ///     Gets a value indicating whether this list is read-only.
    /// </summary>
    /// <remarks>
    ///     This type is a read-only projection; mutation APIs are not supported.
    ///     We still implement <see cref="IList{T}"/> to enable consumers like WinUI's
    ///     <c>Microsoft.UI.Xaml.Controls.ItemsRepeater</c> to use indexed access.
    /// </remarks>
    public bool IsReadOnly => true;

    /// <inheritdoc/>
    bool IList.IsReadOnly => true;

    /// <inheritdoc/>
    bool IList.IsFixedSize => true;

    /// <inheritdoc/>
    int ICollection.Count => this.filtered.Count;

    /// <inheritdoc/>
    bool ICollection.IsSynchronized => false;

    /// <inheritdoc/>
    object ICollection.SyncRoot => this;

    /// <summary>
    ///     Gets the element at the specified index in the filtered view.
    /// </summary>
    /// <param name="index">The zero-based index of the element to get.</param>
    /// <returns>The element at the specified index.</returns>
    /// <exception cref="ArgumentOutOfRangeException">
    ///     If <paramref name="index"/> is outside the bounds of the view.
    /// </exception>
    public T this[int index] => this.filtered[index];

    /// <inheritdoc/>
    object? IList.this[int index]
    {
        get => this.filtered[index];
        set => throw new NotSupportedException("FilteredObservableCollection is read-only.");
    }

    /// <summary>
    ///     Gets or sets the element at the specified index (explicit <see cref="IList{T}"/> implementation).
    /// </summary>
    /// <param name="index">The zero-based index of the element to get or set.</param>
    /// <returns>The element at the specified index when getting.</returns>
    /// <exception cref="NotSupportedException">Always thrown when setting a value.</exception>
    T IList<T>.this[int index]
    {
        get => this.filtered[index];
        set => throw new NotSupportedException("FilteredObservableCollection is read-only.");
    }

    /// <inheritdoc/>
    int IList.Add(object? value) => throw new NotSupportedException("FilteredObservableCollection is read-only.");

    /// <inheritdoc/>
    void IList.Clear() => throw new NotSupportedException("FilteredObservableCollection is read-only.");

    /// <inheritdoc/>
    bool IList.Contains(object? value) => value is T item && this.Contains(item);

    /// <inheritdoc/>
    int IList.IndexOf(object? value) => value is T item ? this.IndexOf(item) : -1;

    /// <inheritdoc/>
    void IList.Insert(int index, object? value) => throw new NotSupportedException("FilteredObservableCollection is read-only.");

    /// <inheritdoc/>
    void IList.Remove(object? value) => throw new NotSupportedException("FilteredObservableCollection is read-only.");

    /// <inheritdoc/>
    void IList.RemoveAt(int index) => throw new NotSupportedException("FilteredObservableCollection is read-only.");

    /// <inheritdoc/>
    void ICollection.CopyTo(Array array, int index)
    {
        ArgumentNullException.ThrowIfNull(array);

        if (array is T[] typed)
        {
            this.CopyTo(typed, index);
            return;
        }

        for (var i = 0; i < this.filtered.Count; i++)
        {
            array.SetValue(this.filtered[i], index + i);
        }
    }

    /// <summary>
    ///     Adds an item to the list (explicit <see cref="ICollection{T}"/> implementation).
    /// </summary>
    /// <param name="item">The item to add.</param>
    /// <exception cref="NotSupportedException">Always thrown.</exception>
    void ICollection<T>.Add(T item) => throw new NotSupportedException("FilteredObservableCollection is read-only.");

    /// <summary>
    ///     Removes all items from the list.
    /// </summary>
    /// <exception cref="NotSupportedException">Always thrown.</exception>
    void ICollection<T>.Clear() => throw new NotSupportedException("FilteredObservableCollection is read-only.");

    /// <summary>
    ///     Determines whether the list contains a specific value.
    /// </summary>
    /// <param name="item">The item to locate in the filtered view.</param>
    /// <returns>True if the item is present; otherwise, false.</returns>
    public bool Contains(T item) => this.filteredSet.Contains(item);

    /// <summary>
    ///     Copies the elements of the list to an array.
    /// </summary>
    /// <param name="array">The destination array.</param>
    /// <param name="arrayIndex">The zero-based index in <paramref name="array"/> at which copying begins.</param>
    public void CopyTo(T[] array, int arrayIndex) => this.filtered.CopyTo(array, arrayIndex);

    /// <summary>
    ///     Removes the first occurrence of a specific object from the list (explicit <see cref="ICollection{T}"/> implementation).
    /// </summary>
    /// <param name="item">The item to remove.</param>
    /// <returns>Always throws <see cref="NotSupportedException"/>.</returns>
    bool ICollection<T>.Remove(T item) => throw new NotSupportedException("FilteredObservableCollection is read-only.");

    /// <summary>
    ///     Determines the index of a specific item in the list.
    /// </summary>
    /// <param name="item">The item to locate.</param>
    /// <returns>The index of the item if found; otherwise, -1.</returns>
    public int IndexOf(T item) => this.filtered.IndexOf(item);

    /// <summary>
    ///     Inserts an item to the list at the specified index (explicit <see cref="IList{T}"/> implementation).
    /// </summary>
    /// <param name="index">The zero-based index at which to insert the item.</param>
    /// <param name="item">The item to insert.</param>
    /// <exception cref="NotSupportedException">Always thrown.</exception>
    void IList<T>.Insert(int index, T item) => throw new NotSupportedException("FilteredObservableCollection is read-only.");

    /// <summary>
    ///     Removes the list item at the specified index (explicit <see cref="IList{T}"/> implementation).
    /// </summary>
    /// <param name="index">The zero-based index of the element to remove.</param>
    /// <exception cref="NotSupportedException">Always thrown.</exception>
    void IList<T>.RemoveAt(int index) => throw new NotSupportedException("FilteredObservableCollection is read-only.");

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

        if (this.useBuilder)
        {
            this.RebuildWithBuilder(resetOnly: true);
        }
        else
        {
            this.filtered.Clear();
            this.filteredSet.Clear();
            foreach (var item in this.source)
            {
                if (this.filter!(item))
                {
                    this.filtered.Add(item);
                    this.filteredSet.Add(item);
                }
            }

            this.RaiseCollectionChanged(new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Reset));
        }
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
        this.Dispose(disposing: true);
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
    private void Dispose(bool disposing)
    {
        if (this.disposed)
        {
            return;
        }

        if (disposing)
        {
            // Unsubscribe from source collection (we always observe source changes)
            this.source.CollectionChanged -= this.OnSourceCollectionChanged;

            // Unsubscribe from ObservedProperties collection notifications
            this.options.ObservedProperties.CollectionChanged -= this.OnObservedPropertiesCollectionChanged;

            // Unsubscribe from any per-item property subscriptions
            foreach (var item in this.propertySubscribedItems.ToList())
            {
                if (item is INotifyPropertyChanged notify)
                {
                    notify.PropertyChanged -= this.OnItemPropertyChanged;
                }
            }

            this.propertySubscribedItems.Clear();
            this.attachedItems.Clear();
            this.filteredSet.Clear();
            this.filtered.Clear();

            this.debounceTimer?.Dispose();
            this.debounceTimer = null;

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

    // We observe item changes only when there is at least one observed property configured.
    private bool IsObservingItemChanges() => this.options.ObservedProperties.Count > 0;

    private void OnObservedPropertiesCollectionChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        // Recompute set of relevant properties
        this.relevantProperties.Clear();
        if (this.options?.ObservedProperties is { } obs)
        {
            foreach (var p in obs)
            {
                this.relevantProperties.Add(p);
            }
        }

        var nowObserving = this.IsObservingItemChanges();

        // If we should observe item changes now, attach handlers for currently attached items
        if (nowObserving)
        {
            foreach (var item in this.attachedItems)
            {
                if (item is INotifyPropertyChanged notify && this.propertySubscribedItems.Add(item))
                {
                    notify.PropertyChanged += this.OnItemPropertyChanged;
                }
            }

            // Enable immediate rebuild so the view immediately reflects current item properties.
            this.RebuildFilteredView(resetOnly: true);
            return;
        }

        // Otherwise ensure we unsubscribe from any existing subscriptions
        foreach (var item in this.propertySubscribedItems.ToList())
        {
            if (item is INotifyPropertyChanged notify)
            {
                notify.PropertyChanged -= this.OnItemPropertyChanged;
            }
        }

        this.propertySubscribedItems.Clear();
    }

    private void HandleAdd(NotifyCollectionChangedEventArgs e)
    {
        if (e.NewItems == null)
        {
            return;
        }

        if (this.useBuilder)
        {
            foreach (T item in e.NewItems)
            {
                this.Attach(item);
            }

            if (this.debounceEnabled)
            {
                this.ScheduleDebouncedRebuild();
            }
            else
            {
                this.RebuildWithBuilder(resetOnly: true);
            }

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

        if (this.useBuilder)
        {
            foreach (T item in e.OldItems)
            {
                this.Detach(item);
            }

            if (this.debounceEnabled)
            {
                this.ScheduleDebouncedRebuild();
            }
            else
            {
                this.RebuildWithBuilder(resetOnly: true);
            }

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
        if (this.useBuilder)
        {
            this.ScheduleDebouncedRebuild();
            return;
        }

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
        if (this.useBuilder)
        {
            if (e.OldItems != null)
            {
                foreach (T oldItem in e.OldItems)
                {
                    this.Detach(oldItem);
                }
            }

            if (e.NewItems != null)
            {
                foreach (T newItem in e.NewItems)
                {
                    this.Attach(newItem);
                }
            }

            if (this.debounceEnabled)
            {
                this.ScheduleDebouncedRebuild();
            }
            else
            {
                this.RebuildWithBuilder(resetOnly: true);
            }

            return;
        }

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

    // Rebuild all subscriptions and the view
    private void HandleReset() => this.RebuildAllFromSource();

    private void Attach(T item)
    {
        // Only subscribe once per item
        if (this.attachedItems.Add(item))
        {
            if (this.IsObservingItemChanges())
            {
                if (item is INotifyPropertyChanged notify)
                {
                    if (this.propertySubscribedItems.Add(item))
                    {
                        notify.PropertyChanged += this.OnItemPropertyChanged;
                    }
                }
            }
        }

        if (this.useBuilder)
        {
            return;
        }

        if (this.filter is not null && this.filter(item))
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
            if (this.propertySubscribedItems.Remove(item))
            {
                if (item is INotifyPropertyChanged notify)
                {
                    notify.PropertyChanged -= this.OnItemPropertyChanged;
                }
            }
        }

        var removed = this.filtered.Remove(item);
        if (removed)
        {
            _ = this.filteredSet.Remove(item);
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

        if (!this.IsObservingItemChanges())
        {
            return;
        }

        var isRelevant = string.IsNullOrEmpty(e.PropertyName) || this.relevantProperties.Contains(e.PropertyName);
        if (!isRelevant)
        {
            return;
        }

        if (this.debounceEnabled)
        {
            // Accumulate changed items while coalescing; the debounced handler will process them incrementally for predicate-based views.
            this.dirtyItems.Add(item);
            this.ScheduleDebouncedRebuild();
            return;
        }

        if (this.useBuilder)
        {
            this.RebuildWithBuilder(resetOnly: true);
            return;
        }

        var inFiltered = this.filteredSet.Contains(item);
        var shouldBeIn = this.filter!(item);

        if (inFiltered && !shouldBeIn)
        {
            var index = this.filtered.IndexOf(item);
            if (index >= 0)
            {
                this.filtered.RemoveAt(index);
                _ = this.filteredSet.Remove(item);
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

    private void ScheduleDebouncedRebuild()
    {
        if (!this.debounceEnabled)
        {
            this.RebuildFilteredView(resetOnly: true);
            return;
        }

        using (this.debounceGate.EnterScope())
        {
            this.debouncePending = true;

            if (this.debounceTimer is null)
            {
                this.debounceTimer = new Timer(
                    this.OnDebounceTimer,
                    state: null,
                    dueTime: this.propertyChangedDebounceInterval,
                    period: Timeout.InfiniteTimeSpan);
                return;
            }

            // Always push the due time so multiple rapid changes coalesce into a single rebuild after the last change.
            _ = this.debounceTimer.Change(this.propertyChangedDebounceInterval, Timeout.InfiniteTimeSpan);
        }
    }

    private void OnDebounceTimer(object? state)
    {
        if (this.disposed)
        {
            return;
        }

        void Dispatch()
        {
            if (this.disposed)
            {
                return;
            }

            using (this.debounceGate.EnterScope())
            {
                if (!this.debouncePending)
                {
                    return;
                }

                this.debouncePending = false;
            }

            // If we're using a builder the safe default is to perform a reset rebuild; otherwise
            // we can process the set of dirty items accumulated during the debounce window
            // and apply incremental adds/removes to minimize Reset events.
            if (this.useBuilder)
            {
                this.RebuildFilteredView(resetOnly: true);
                return;
            }

            // Incrementally process each dirty item and apply per-item diffs
            var itemsToProcess = this.dirtyItems.ToList();
            this.dirtyItems.Clear();
            foreach (var item in itemsToProcess)
            {
                var inFiltered = this.filteredSet.Contains(item);
                var shouldBeIn = this.filter!(item);

                if (inFiltered && !shouldBeIn)
                {
                    var index = this.filtered.IndexOf(item);
                    if (index >= 0)
                    {
                        this.filtered.RemoveAt(index);
                        _ = this.filteredSet.Remove(item);
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

        if (this.synchronizationContext is not null)
        {
            this.synchronizationContext.Post(_ => Dispatch(), state: null);
            return;
        }

        Dispatch();
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

    private void RebuildFilteredView(bool resetOnly)
    {
        if (this.useBuilder)
        {
            this.RebuildWithBuilder(resetOnly);
            return;
        }

        if (this.filter is null)
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

        if (resetOnly)
        {
            this.RaiseCollectionChanged(new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Reset));
        }
    }

    /// <summary>
    /// Rebuilds all subscriptions and the filtered view from the current source contents
    /// and raises Reset. Used for Reset actions on the source collection.
    /// </summary>
    private void RebuildAllFromSource()
    {
        // Unsubscribe from all previously attached item property handlers
        foreach (var item in this.propertySubscribedItems.ToList())
        {
            if (item is INotifyPropertyChanged notify)
            {
                notify.PropertyChanged -= this.OnItemPropertyChanged;
            }
        }

        this.propertySubscribedItems.Clear();
        this.attachedItems.Clear();
        this.filtered.Clear();
        this.filteredSet.Clear();

        foreach (var item in this.source)
        {
            this.Attach(item);
        }

        if (this.useBuilder)
        {
            this.RebuildWithBuilder(resetOnly: true);
        }
        else
        {
            this.RaiseCollectionChanged(new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Reset));
        }
    }

    private void RebuildWithBuilder(bool resetOnly)
    {
        if (this.disposed || this.viewBuilder is null)
        {
            return;
        }

        // Compute outside notification scope to keep a single Reset emission.
        var view = this.viewBuilder.Build(this.source);

        this.filtered.Clear();
        this.filteredSet.Clear();

        foreach (var item in view)
        {
            this.filtered.Add(item);
            this.filteredSet.Add(item);
        }

        if (resetOnly)
        {
            this.RaiseCollectionChanged(new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Reset));
        }
    }

    /// <summary>
    ///     A lightweight <see cref="IDisposable"/> used by <see cref="DeferNotifications"/>
    ///     to defer and then resume collection change notifications.
    /// </summary>
    /// <param name="owner">The owning <see cref="FilteredObservableCollection{T}"/> instance.</param>
    private readonly struct NotificationSuspender(FilteredObservableCollection<T> owner) : IDisposable
    {
        /// <inheritdoc/>
        public void Dispose() => owner.ResumeNotifications();
    }
}
