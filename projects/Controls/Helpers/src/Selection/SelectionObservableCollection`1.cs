// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Collections.Specialized;

/// <summary>
/// A custom <see cref="ObservableCollection{T}" /> that, if its items are of type <see cref="ISelectable" />, will automatically
/// manage their <see cref="ISelectable.IsSelected" /> property when added to or removed from the collection.
/// </summary>
/// <typeparam name="T">The type of items in the collection.</typeparam>
/// <param name="collection">The collection from whihc the initial elements in the observable collection are copied.</param>
internal sealed partial class SelectionObservableCollection<T>(IEnumerable<int> collection)
    : ObservableCollection<int>(collection)
{
    private bool suppressNotification;

    public delegate T ItemGetter(int value);

    public required ItemGetter GetItemAt { get; init; }

    public IDisposable SuspendNotifications() => new NotificationSuspender(this);

    protected override void OnCollectionChanged(NotifyCollectionChangedEventArgs e)
    {
        if (!this.suppressNotification)
        {
            base.OnCollectionChanged(e);
        }
    }

    /// <inheritdoc />
    /// <remarks>
    /// Ensures that the collection does not contain duplicate items.
    /// </remarks>
    protected override void InsertItem(int index, int item)
    {
        if (this.Contains(item))
        {
            return;
        }

        if (this.GetItemAt(item) is ISelectable treeItem)
        {
            treeItem.IsSelected = true;
        }

        base.InsertItem(index, item);
    }

    /// <inheritdoc />
    protected override void SetItem(int index, int item)
    {
        if (this.GetItemAt(item) is ISelectable oldTreeItem)
        {
            oldTreeItem.IsSelected = false;
        }

        base.SetItem(index, item);

        if (this.GetItemAt(item) is ISelectable newTreeItem)
        {
            newTreeItem.IsSelected = false;
        }
    }

    protected override void ClearItems()
    {
        foreach (var item in this)
        {
            if (this.GetItemAt(item) is ISelectable treeItem)
            {
                treeItem.IsSelected = false;
            }
        }

        base.ClearItems();
    }

    protected override void RemoveItem(int index)
    {
        var item = this[index];
        if (this.GetItemAt(item) is ISelectable treeItem)
        {
            treeItem.IsSelected = false;
        }

        base.RemoveItem(index);
    }

    private sealed partial class NotificationSuspender : IDisposable
    {
        private readonly SelectionObservableCollection<T> collection;

        public NotificationSuspender(SelectionObservableCollection<T> collection)
        {
            this.collection = collection;
            collection.suppressNotification = true;
        }

        public void Dispose()
        {
            this.collection.suppressNotification = false;
            this.collection.OnCollectionChanged(
                new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Reset));
        }
    }
}
