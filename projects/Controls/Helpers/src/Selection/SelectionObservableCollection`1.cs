// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Collections.Specialized;

internal sealed class SelectionObservableCollection<T>(IEnumerable<int> collection)
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

    private sealed class NotificationSuspender : IDisposable
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
