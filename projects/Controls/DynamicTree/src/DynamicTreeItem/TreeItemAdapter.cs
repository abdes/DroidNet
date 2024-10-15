// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

using System;
using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.Diagnostics;
using CommunityToolkit.Mvvm.ComponentModel;

/// <summary>
/// Represents an abstract base class for tree items in a dynamic tree structure.
/// </summary>
/// <remarks>
/// The <see cref="TreeItemAdapter" /> class provides the foundational implementation for tree items, including properties and
/// methods for managing child items, parent references, and event handling. It extends the <see cref="ObservableObject" /> class
/// and implements the <see cref="ITreeItem" /> interface.
/// </remarks>
public abstract partial class TreeItemAdapter : ObservableObject, ITreeItem
{
    /// <summary>
    /// Represents the internal collection of child items in the tree. This collection is lazily initialized.
    /// </summary>
    /// <seealso cref="childrenLazy" />
    /// <seealso cref="InitializeChildrenCollectionAsync" />
    private readonly ObservableCollection<ITreeItem> children = [];

    /// <summary>
    /// Lazily initialized read-only view of the collection of child items.
    /// </summary>
    private readonly Lazy<Task<ReadOnlyObservableCollection<ITreeItem>>> childrenLazy;

    /// <summary>
    /// Indicates whether the tree item is expanded.
    /// </summary>
    [ObservableProperty]
    private bool isExpanded;

    /// <summary>
    /// Indicates whether the tree item is selected.
    /// </summary>
    [ObservableProperty]
    private bool isSelected;

    /// <summary>
    /// Indicates whether the tree item is locked.
    /// <remarks>
    /// When <see langword="true" />, the item cannot be deleted or moved within the tree.
    /// </remarks>
    /// </summary>
    [ObservableProperty]
    private bool isLocked;

    /// <summary>
    /// Initializes a new instance of the <see cref="TreeItemAdapter" /> class.
    /// </summary>
    protected TreeItemAdapter()
    {
        this.childrenLazy
            = new Lazy<Task<ReadOnlyObservableCollection<ITreeItem>>>(this.InitializeChildrenCollectionAsync);
        this.children.CollectionChanged += (_, args) => this.ChildrenCollectionChanged?.Invoke(this, args);
    }

    /// <summary>
    /// Occurs when the collection of child items changes.
    /// </summary>
    public event EventHandler<NotifyCollectionChangedEventArgs>? ChildrenCollectionChanged;

    /// <inheritdoc />
    public virtual bool IsRoot => this.Parent is null;

    /// <inheritdoc />
    public abstract string Label { get; }

    /// <inheritdoc />
    public ITreeItem? Parent { get; private set; }

    /// <summary>
    /// Gets the collection of child items.
    /// </summary>
    /// <remarks>
    /// The collection of child items is lazily initialized. The first call to this property getter will trigger the loading
    /// of the collection items through the <seealso cref="LoadChildren" />.
    /// </remarks>
    public Task<ReadOnlyObservableCollection<ITreeItem>> Children => this.childrenLazy.Value;

    public int ChildrenCount => this.childrenLazy.IsValueCreated
        ? this.children.Count
        : this.GetChildrenCount();

    /// <inheritdoc />
    public int Depth => this.Parent is null ? -1 : this.Parent.Depth + 1;

    /// <summary>
    /// Adds a child item asynchronously.
    /// </summary>
    /// <param name="child">The child item to add.</param>
    /// <returns>A task representing the asynchronous operation.</returns>
    /// <exception cref="ArgumentException">Thrown when the item does not derive from <see cref="TreeItemAdapter" />.</exception>
    public async Task AddChildAsync(ITreeItem child)
        => await this.ManipulateChildrenAsync(this.AddChildInternal, child).ConfigureAwait(false);

    /// <summary>
    /// Inserts a child item at the specified index asynchronously.
    /// </summary>
    /// <param name="index">The index at which to insert the child item.</param>
    /// <param name="child">The child item to insert.</param>
    /// <returns>A task representing the asynchronous operation.</returns>
    /// <exception cref="ArgumentException">Thrown when the item does not derive from <see cref="TreeItemAdapter" />.</exception>
    public async Task InsertChildAsync(int index, ITreeItem child)
        => await this.ManipulateChildrenAsync(
                (item) =>
                {
                    this.children.Insert(index, item);
                    item.Parent = this;
                },
                child)
            .ConfigureAwait(false);

    /// <summary>
    /// Removes a child item asynchronously.
    /// </summary>
    /// <param name="child">The child item to remove.</param>
    /// <returns>A task representing the asynchronous operation.</returns>
    /// <exception cref="ArgumentException">Thrown when the item does not derive from <see cref="TreeItemAdapter" />.</exception>
    public async Task RemoveChildAsync(ITreeItem child)
        => await this.ManipulateChildrenAsync(
                (item) =>
                {
                    if (this.children.Remove(item))
                    {
                        item.Parent = null;
                    }
                },
                child)
            .ConfigureAwait(false);

    /// <summary>
    /// Gets the count of child tree items asynchronously.
    /// </summary>
    /// <returns>The count of child tree items.</returns>
    /// <remarks>
    /// Due to the potential that getting the collection of all child items may be very expensive, it is suggested that whenever
    /// possible, getting the count of children items should be implemented in a lightweight and efficient manner that does not
    /// require a full update of the <seealso cref="Children" /> collection.
    /// </remarks>
    protected abstract int GetChildrenCount();

    /// <summary>
    /// Loads the child items asynchronously. Used to lazily initialize the collection of child items.
    /// </summary>
    /// <returns>A task representing the asynchronous operation.</returns>
    /// <remarks>
    /// Loading of child items on demand allows for reducing the workload when displaying the tree. Only expanded items
    /// needs their children to be loaded.
    /// </remarks>
    protected abstract Task LoadChildren();

    /// <summary>
    /// Remove all child elements from the collection of children.
    /// </summary>
    protected void ClearChildren() => this.children.Clear();

    /// <summary>
    /// Adds a child item to the children collection synchronously. Used internally, and by derived classes, to populate the
    /// collection when items are already being loaded. Use <see cref="AddChildAsync" /> for regular operations to add a child
    /// item.
    /// </summary>
    /// <param name="item">The child item to add.</param>
    protected void AddChildInternal(TreeItemAdapter item)
    {
        this.children.Add(item);
        item.Parent = this;
    }

    /// <summary>
    /// Manipulates the child items asynchronously.
    /// </summary>
    /// <param name="action">The action to perform on the child item.</param>
    /// <param name="item">The child item to manipulate.</param>
    /// <returns>A task representing the asynchronous operation.</returns>
    /// <exception cref="ArgumentException">Thrown when the item does not derive from <see cref="TreeItemAdapter" />.</exception>
    private async Task ManipulateChildrenAsync(Action<TreeItemAdapter> action, ITreeItem item)
    {
        if (item is TreeItemAdapter adapter)
        {
            _ = await this.Children.ConfigureAwait(false);
            action(adapter);
        }
        else
        {
            throw new ArgumentException(
                $"item has a type `{item.GetType()}` that not derive from `{typeof(TreeItemAdapter)}`",
                nameof(item));
        }
    }

    /// <summary>
    /// Initializes the collection of child items asynchronously.
    /// </summary>
    /// <returns>A task representing the asynchronous operation.</returns>
    private async Task<ReadOnlyObservableCollection<ITreeItem>> InitializeChildrenCollectionAsync()
    {
        Debug.Assert(
            this.children.Count == 0,
            "Ensure that the children collection is loaded before you add things to it");

        await this.LoadChildren().ConfigureAwait(false);
        return new ReadOnlyObservableCollection<ITreeItem>(this.children);
    }
}
