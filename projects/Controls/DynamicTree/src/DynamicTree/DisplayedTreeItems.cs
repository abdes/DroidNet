// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections;
using System.Collections.Specialized;

namespace DroidNet.Controls;

/// <summary>
/// The rows a <see cref="DynamicTree"/> shows: a read-only view over its view model's
/// <see cref="DynamicTreeViewModel.ShownItems"/> or <see cref="DynamicTreeViewModel.FilteredItems"/>. The tree's
/// <c>ItemsRepeater</c> is bound to one instance of this view for the tree's whole life, and the tree decides when the
/// view passes the rows' changes on.
/// </summary>
/// <remarks>
/// <para>
/// This view exists because of two failures of binding the repeater to the view model's collection directly.
/// </para>
/// <para>
/// A view model can outlive the trees that show it: the Content Browser, for instance, rebuilds its view over the same
/// view model. An <c>ItemsRepeater</c> subscribes to the <see cref="INotifyCollectionChanged"/> of its source and never
/// lets go, so every discarded tree kept listening to the rows. When the rows were reset, a discarded repeater failed
/// the notification with <c>E_FAIL</c>, and that failure stopped the collection from notifying the live tree, which
/// then showed stale rows.
/// </para>
/// <para>
/// Clearing the repeater's source when the tree unloads fixed that, but it is a XAML property change, and a window
/// that is closing raises <c>Unloaded</c> while its XAML is being torn down: the change threw a
/// <c>COMException</c> and crashed the application on exit.
/// </para>
/// <para>
/// So the repeater keeps this view as its source, and an unloaded tree calls <see cref="Detach"/>, which only removes
/// this view's own handler from the rows: no XAML is touched, so it cannot fail during teardown, and the view model no
/// longer holds the discarded repeater. When the tree is loaded again, <see cref="Attach"/> listens again and raises a
/// reset, because the repeater missed every change in between.
/// </para>
/// </remarks>
internal sealed partial class DisplayedTreeItems : IList, IReadOnlyList<ITreeItem>, INotifyCollectionChanged
{
    private static readonly NotifyCollectionChangedEventArgs ResetArgs = new(NotifyCollectionChangedAction.Reset);

    private IList? rows;
    private INotifyCollectionChanged? observed;

    /// <inheritdoc/>
    public event NotifyCollectionChangedEventHandler? CollectionChanged;

    /// <summary>Gets the rows this view shows, or <see langword="null"/> when the tree has no view model.</summary>
    public IList? Source => this.rows;

    /// <summary>
    /// Gets a value indicating whether the view passes the rows' changes on to the repeater: true while the tree is
    /// loaded, false once it unloaded.
    /// </summary>
    public bool IsAttached => this.observed is not null;

    /// <inheritdoc/>
    public int Count => this.Items.Count;

    /// <inheritdoc/>
    public bool IsReadOnly => true;

    /// <inheritdoc/>
    public bool IsFixedSize => false;

    /// <inheritdoc/>
    public bool IsSynchronized => false;

    /// <inheritdoc/>
    public object SyncRoot => this;

    // What the view reads. It stays readable while detached: an unloaded repeater does not read it, and the reset that
    // Attach raises makes a reloaded one read it afresh.
    private IList Items => this.rows ?? Array.Empty<ITreeItem>();

    /// <inheritdoc/>
    object? IList.this[int index]
    {
        get => this.Items[index];
        set => throw new NotSupportedException("The displayed rows are read-only.");
    }

    /// <inheritdoc/>
    ITreeItem IReadOnlyList<ITreeItem>.this[int index] => (ITreeItem)this.Items[index]!;

    /// <summary>
    /// Shows these rows and passes their changes on, raising a reset so the repeater rebuilds from them. The tree calls
    /// it when it loads, when its view model changes and when filtering switches between shown and filtered rows.
    /// </summary>
    /// <param name="rows">The rows, or <see langword="null"/> to show none.</param>
    public void Attach(IList? rows)
    {
        // Already showing and listening to these rows: a reset would only make the repeater rebuild every row.
        if (ReferenceEquals(rows, this.rows) && this.IsAttached)
        {
            return;
        }

        this.Detach();
        this.rows = rows;
        this.observed = rows as INotifyCollectionChanged ?? NotObservable.Instance;
        this.observed.CollectionChanged += this.OnSourceChanged;
        this.CollectionChanged?.Invoke(this, ResetArgs);
    }

    /// <summary>
    /// Stops passing the rows' changes on. It raises nothing and touches no XAML, because the tree calls it from
    /// <c>Unloaded</c>, which a closing window raises while its XAML is being torn down.
    /// </summary>
    public void Detach()
    {
        if (this.observed is { } current)
        {
            current.CollectionChanged -= this.OnSourceChanged;
            this.observed = null;
        }
    }

    /// <inheritdoc/>
    public bool Contains(object? value) => this.Items.Contains(value);

    /// <inheritdoc/>
    public int IndexOf(object? value) => this.Items.IndexOf(value);

    /// <inheritdoc/>
    public void CopyTo(Array array, int index) => this.Items.CopyTo(array, index);

    /// <inheritdoc/>
    public IEnumerator GetEnumerator() => this.Items.GetEnumerator();

    /// <inheritdoc/>
    IEnumerator<ITreeItem> IEnumerable<ITreeItem>.GetEnumerator() => this.Items.Cast<ITreeItem>().GetEnumerator();

    /// <inheritdoc/>
    public int Add(object? value) => throw new NotSupportedException("The displayed rows are read-only.");

    /// <inheritdoc/>
    public void Clear() => throw new NotSupportedException("The displayed rows are read-only.");

    /// <inheritdoc/>
    public void Insert(int index, object? value) => throw new NotSupportedException("The displayed rows are read-only.");

    /// <inheritdoc/>
    public void Remove(object? value) => throw new NotSupportedException("The displayed rows are read-only.");

    /// <inheritdoc/>
    public void RemoveAt(int index) => throw new NotSupportedException("The displayed rows are read-only.");

    private void OnSourceChanged(object? sender, NotifyCollectionChangedEventArgs args) => this.CollectionChanged?.Invoke(this, args);

    /// <summary>
    /// Stands in for rows that raise no changes, such as the empty rows of a tree without a view model, so that
    /// <see cref="IsAttached"/> means the same for every source.
    /// </summary>
    private sealed class NotObservable : INotifyCollectionChanged
    {
        public static readonly NotObservable Instance = new();

        public event NotifyCollectionChangedEventHandler? CollectionChanged
        {
            add { }
            remove { }
        }
    }
}
