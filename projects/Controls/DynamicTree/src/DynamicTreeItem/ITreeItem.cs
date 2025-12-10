// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Collections.Specialized;
using DroidNet.Controls.Selection;

namespace DroidNet.Controls;

/// <summary>
///     Represents an item in a hierarchical tree structure, providing functionality for managing child items,
///     tracking selection state, and handling expansion and collapse states.
/// </summary>
public interface ITreeItem : ISelectable, ICanBeLocked
{
    /// <summary>
    ///     Occurs when the collection of child items changes.
    /// </summary>
    /// <seealso cref="ObservableCollection{T}.CollectionChanged" />
    public event EventHandler<NotifyCollectionChangedEventArgs> ChildrenCollectionChanged;

    /// <summary>
    ///     Gets or sets the label of the tree item, used when presenting the item content with the default styling.
    /// </summary>
    public string Label { get; set; }

    /// <summary>
    ///     Gets or sets a value indicating whether the tree item is expanded.
    /// </summary>
    public bool IsExpanded { get; set; }

    /// <summary>
    ///     Gets a value indicating whether the tree item is the root. Defaults to <see langword="false" />, unless otherwise
    ///     set at
    ///     creation.
    ///     <remarks>
    ///         The root of the tree is always <see cref="ICanBeLocked.IsLocked">locked</see>, protecting it from being moved
    ///         or deleted.
    ///         Once an item is marked as the root of a tree, it will stay the root forever.
    ///     </remarks>
    /// </summary>
    public bool IsRoot { get; init; }

    /// <summary>
    ///     Gets the parent tree item.
    /// </summary>
    public ITreeItem? Parent { get; }

    /// <summary>
    ///     Gets the depth of the tree item in the hierarchy.
    /// </summary>
    public int Depth { get; }

    /// <summary>
    ///     Gets the collection of child tree items.
    /// </summary>
    public Task<ReadOnlyObservableCollection<ITreeItem>> Children { get; }

    /// <summary>
    ///     Gets the count of child items under this tree item.
    /// </summary>
    /// <value>
    ///     The number of child tree items.
    /// </value>
    /// <remarks>
    ///     This property provides a quick way to retrieve the count of child items without the need to access the entire
    ///     collection.
    ///     If the collection has not been loaded yet, a pr-load count is obtained without loading the items, and is used until
    ///     the
    ///     collection is loaded. At that point, the count should be the number of items in the <see cref="Children" />
    ///     collection.
    /// </remarks>
    public int ChildrenCount { get; }

    /// <summary>
    ///     Gets a value indicating whether the tree item can accept child items.
    /// </summary>
    public bool CanAcceptChildren { get; }

    /// <summary>
    ///     Validates the given <paramref name="name" />.
    /// </summary>
    /// <param name="name">The proposed name to be validated.</param>
    /// <returns><see langword="true" /> if the <paramref name="name" /> is valid; <see langword="false" /> otherwise.</returns>
    public bool ValidateItemName(string name);

    /// <summary>
    ///     Adds a child tree item asynchronously.
    /// </summary>
    /// <param name="child">The child tree item to add.</param>
    /// <returns>A task that represents the asynchronous operation.</returns>
    public Task AddChildAsync(ITreeItem child);

    /// <summary>
    ///     Inserts a child tree item at the specified index asynchronously.
    /// </summary>
    /// <param name="index">The zero-based index at which the child tree item should be inserted.</param>
    /// <param name="child">The child tree item to insert.</param>
    /// <returns>A task that represents the asynchronous operation.</returns>
    public Task InsertChildAsync(int index, ITreeItem child);

    /// <summary>
    ///     Removes a child tree item asynchronously.
    /// </summary>
    /// <param name="child">The child tree item to remove.</param>
    /// <returns>
    ///     A task that represents the asynchronous operation, holding the index of removed child, or `-1` if the item was not
    ///     found.
    /// </returns>
    public Task<int> RemoveChildAsync(ITreeItem child);
}

/// <summary>
///     A <see cref="ITreeItem" /> that holds a reference to an application specific object of type
///     <typeparamref name="T" /> or any
///     type derived from it.
/// </summary>
/// <typeparam name="T">
///     The type of the object that can be attached to this <see cref="ITreeItem{T}" />.
///     <remarks>
///         The out keyword specifies that the type parameter T is covariant. Covariance allows us to use a more derived
///         type than
///         originally specified by the generic parameter. This is particularly useful in scenarios where we want to ensure
///         that a type
///         can be safely substituted with its derived types, making the interface more flexible and allows greater reuse
///         of code with
///         different types.
///     </remarks>
/// </typeparam>
/// <remarks>
///     This is a type-safe specialization of the generic <see cref="ITreeItem" /> interface, for use by implementations of
///     custom
///     <see cref="TreeItemAdapter" /> classes. The <see cref="DynamicTree" /> does not care about the type of object held
///     by
///     the tree item, and it only manipulates it as a generic <see cref="ITreeItem" />.
/// </remarks>
public interface ITreeItem<out T> : ITreeItem
{
    /// <summary>
    ///     Gets the application-specific object of type <typeparamref name="T" /> that is attached to this tree item.
    /// </summary>
    /// <value>
    ///     The object of type <typeparamref name="T" /> that is associated with this tree item.
    /// </value>
    public T AttachedObject { get; }
}
