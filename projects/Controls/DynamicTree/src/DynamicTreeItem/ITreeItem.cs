// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

using System.Collections.ObjectModel;
using System.Collections.Specialized;

/// <summary>
/// Represents an item in a hierarchical tree structure, providing functionality for managing child items,
/// tracking selection state, and handling expansion and collapse states.
/// </summary>
public interface ITreeItem : ISelectable
{
    /// <summary>
    /// Occurs when the collection of child items changes.
    /// </summary>
    /// <seealso cref="ObservableCollection{T}.CollectionChanged" />
    event EventHandler<NotifyCollectionChangedEventArgs> ChildrenCollectionChanged;

    /// <summary>
    /// Gets the label of the tree item, used when presenting the item content with the default styling.
    /// </summary>
    string Label { get; }

    /// <summary>
    /// Gets or sets a value indicating whether the tree item is expanded.
    /// </summary>
    bool IsExpanded { get; set; }

    /// <summary>
    /// Gets a value indicating whether the tree item is the root.
    /// </summary>
    bool IsRoot { get; }

    /// <summary>
    /// Gets the parent tree item.
    /// </summary>
    ITreeItem? Parent { get; }

    /// <summary>
    /// Gets the collection of child tree items.
    /// </summary>
    Task<ReadOnlyObservableCollection<ITreeItem>> Children { get; }

    /// <summary>
    /// Gets the depth of the tree item in the hierarchy.
    /// </summary>
    int Depth { get; }

    /// <summary>
    /// Adds a child tree item asynchronously.
    /// </summary>
    /// <param name="child">The child tree item to add.</param>
    /// <returns>A task that represents the asynchronous operation.</returns>
    Task AddChildAsync(ITreeItem child);

    /// <summary>
    /// Inserts a child tree item at the specified index asynchronously.
    /// </summary>
    /// <param name="index">The zero-based index at which the child tree item should be inserted.</param>
    /// <param name="child">The child tree item to insert.</param>
    /// <returns>A task that represents the asynchronous operation.</returns>
    Task InsertChildAsync(int index, ITreeItem child);

    /// <summary>
    /// Removes a child tree item asynchronously.
    /// </summary>
    /// <param name="child">The child tree item to remove.</param>
    /// <returns>A task that represents the asynchronous operation.</returns>
    Task RemoveChildAsync(ITreeItem child);

    /// <summary>
    /// Gets the count of child tree items asynchronously.
    /// </summary>
    /// <returns>A task that represents the asynchronous operation, containing the count of child tree items.</returns>
    /// <remarks>
    /// Due to the potential that getting the collection of all child items may be very expensive, it is suggested that whenever
    /// possible, getting the count of children items should be implemented in a lightweight and efficient manner that does not
    /// require a full update of the <seealso cref="Children" /> collection.
    /// </remarks>
    Task<int> GetChildrenCountAsync();
}

/// <summary>
/// A <see cref="ITreeItem" /> that holds a reference to an application specific object of type <typeparamref name="T" /> or any
/// type derived from it.
/// </summary>
/// <typeparam name="T">
/// The type of the object that can be attached to this <see cref="ITreeItem{T}" />.
/// <remarks>
/// The out keyword specifies that the type parameter T is covariant. Covariance allows us to use a more derived type than
/// originally specified by the generic parameter. This is particularly useful in scenarios where we want to ensure that a type
/// can be safely substituted with its derived types, making the interface more flexible and allows greater reuse of code with
/// different types.
/// </remarks>
/// </typeparam>
/// <remarks>
/// This is a type-safe specialization of the generic <see cref="ITreeItem" /> interface, for use by implementations of custom
/// <see cref="TreeItemAdapter" /> classes. The <see cref="DynamicTree" /> does not care about the type of object held by
/// the tree item, and it only manipulates it as a generic <see cref="ITreeItem" />.
/// </remarks>
public interface ITreeItem<out T> : ITreeItem
{
    /// <summary>
    /// Gets the application-specific object of type <typeparamref name="T" /> that is attached to this tree item.
    /// </summary>
    /// <value>
    /// The object of type <typeparamref name="T" /> that is associated with this tree item.
    /// </value>
    public T AttachedObject { get; }
}
