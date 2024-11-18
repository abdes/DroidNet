// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Collections.Specialized;

namespace DroidNet.Controls.Selection;

/// <summary>
/// A custom <see cref="ObservableCollection{T}" /> that manages a collection of selected indices
/// and automatically updates the <see cref="ISelectable.IsSelected" /> property of items in the
/// underlying data model.
/// </summary>
/// <typeparam name="T">The type of items in the underlying data model.</typeparam>
/// <param name="collection">
/// An <see cref="IEnumerable{T}"/> of indices representing the initially selected items.
/// These indices correspond to the positions of items in the underlying data model.
/// </param>
/// <remarks>
/// <para>
/// The <see cref="SelectionObservableCollection{T}"/> class extends <see cref="ObservableCollection{T}"/>
/// (of <see langword="int"/>) to maintain a collection of selected item indices.
/// </para>
/// <para>
/// This class ensures that items in the underlying data model, which implement the <see cref="ISelectable"/>
/// interface, have their <see cref="ISelectable.IsSelected"/> property automatically updated when
/// their corresponding index is added to or removed from the collection.
/// </para>
/// <para>
/// It prevents duplication of indices and provides synchronization between the selection state and
/// the underlying items.
/// </para>
/// </remarks>
/// <example>
/// <para><strong>Example Usage:</strong></para>
/// <code><![CDATA[
/// public class MyItem : ISelectable
/// {
/// public bool IsSelected { get; set; }
/// public string Name { get; set; }
/// }
///
/// var items = new List<MyItem>
/// {
/// new MyItem { Name = "Item 1" },
/// new MyItem { Name = "Item 2" },
/// new MyItem { Name = "Item 3" }
/// };
///
/// var collection = new SelectionObservableCollection<MyItem>(items.Select((item, index) => index))
/// {
/// GetItemAt = index => items[index]
/// };
///
/// collection.Add(1); // Item 2 is now selected
/// collection.Remove(1); // Item 2 is now deselected
///
/// // When doing bulk updates, it is recommended to suspend notifications to avoid a burst of change notifications.
/// using (selectionCollection.SuspendNotifications())
/// {
///     // Add multiple indices to select items
///     selectionCollection.Add(1); // Selects Item 2
///     selectionCollection.Add(3); // Selects Item 4
///     selectionCollection.Add(0); // Selects Item 1
/// }
/// ]]></code>
/// </example>
internal sealed partial class SelectionObservableCollection<T>(IEnumerable<int> collection)
    : ObservableCollection<int>(collection)
{
    private bool suppressNotification;

    /// <summary>
    /// Delegate to get the item at a specific index.
    /// </summary>
    /// <param name="value">The index of the item.</param>
    /// <returns>The item at the specified index.</returns>
    public delegate T ItemGetter(int value);

    /// <summary>
    /// Gets the delegate to retrieve an item at a specific index.
    /// </summary>
    public required ItemGetter GetItemAt { get; init; }

    /// <summary>
    /// Suspends notifications for collection changes.
    /// </summary>
    /// <returns>An <see cref="IDisposable"/> object that, when disposed, resumes notifications.</returns>
    /// <remarks>
    /// When notifications are suspended, changes to the collection will not raise the
    /// <see cref="ObservableCollection{T}.CollectionChanged"/> event. This can be useful for performing
    /// bulk updates to the collection without triggering multiple change notifications. When the
    /// returned <see cref="IDisposable"/> object is disposed, a single <see cref="NotifyCollectionChangedAction.Reset"/>
    /// event is raised to indicate that the collection has changed.
    /// </remarks>
    public IDisposable SuspendNotifications() => new NotificationSuspender(this);

    /// <inheritdoc/>
    protected override void OnCollectionChanged(NotifyCollectionChangedEventArgs e)
    {
        if (!this.suppressNotification)
        {
            base.OnCollectionChanged(e);
        }
    }

    /// <inheritdoc />
    /// <remarks>
    /// Ensures that the collection does not contain duplicate items. When an item is added to the
    /// collection, its <see cref="ISelectable.IsSelected"/> property is set to <see langword="true"/>.
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
    /// <remarks>
    /// When an item is replaced in the collection, the <see cref="ISelectable.IsSelected"/> property of the old item is set to <see langword="false"/>,
    /// and the <see cref="ISelectable.IsSelected"/> property of the new item is set to <see langword="true"/>.
    /// </remarks>
    protected override void SetItem(int index, int item)
    {
        if (this.GetItemAt(this[index]) is ISelectable oldTreeItem)
        {
            oldTreeItem.IsSelected = false;
        }

        base.SetItem(index, item);

        if (this.GetItemAt(item) is ISelectable newTreeItem)
        {
            newTreeItem.IsSelected = true;
        }
    }

    /// <inheritdoc/>
    /// <remarks>
    /// When the collection is cleared, the <see cref="ISelectable.IsSelected"/> property of all items in the collection is set to <see langword="false"/>.
    /// </remarks>
    protected override void ClearItems()
    {
        foreach (var index in this)
        {
            if (this.GetItemAt(index) is ISelectable treeItem)
            {
                treeItem.IsSelected = false;
            }
        }

        base.ClearItems();
    }

    /// <inheritdoc/>
    /// <remarks>
    /// When an item is removed from the collection, its <see cref="ISelectable.IsSelected"/> property
    /// is set to <see langword="false"/>.
    /// </remarks>
    protected override void RemoveItem(int index)
    {
        var item = this[index];
        if (this.GetItemAt(item) is ISelectable treeItem)
        {
            treeItem.IsSelected = false;
        }

        base.RemoveItem(index);
    }

    /// <summary>
    /// A helper class to suspend and resume notifications for collection changes.
    /// </summary>
    /// <remarks>
    /// When an instance of this class is created, it suspends notifications for collection changes.
    /// When the instance is disposed, it resumes notifications and raises a single
    /// <see cref="NotifyCollectionChangedAction.Reset"/> event to indicate that the collection has changed.
    /// </remarks>
    private sealed partial class NotificationSuspender : IDisposable
    {
        private readonly SelectionObservableCollection<T> collection;

        /// <summary>
        /// Initializes a new instance of the <see cref="NotificationSuspender"/> class.
        /// </summary>
        /// <param name="collection">The collection for which notifications are to be suspended.</param>
        public NotificationSuspender(SelectionObservableCollection<T> collection)
        {
            this.collection = collection;
            collection.suppressNotification = true;
        }

        /// <summary>
        /// Resumes notifications for collection changes and raises a <see cref="NotifyCollectionChangedAction.Reset"/> event.
        /// </summary>
        public void Dispose()
        {
            this.collection.suppressNotification = false;
            this.collection.OnCollectionChanged(
                new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Reset));
        }
    }
}
