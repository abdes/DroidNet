// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Collections.Specialized;
using System.Diagnostics;
using CommunityToolkit.Mvvm.ComponentModel;

namespace DroidNet.Controls;

/// <summary>
///     Represents an abstract base class for tree items in a dynamic tree structure.
/// </summary>
/// <remarks>
///     The <see cref="TreeItemAdapter" /> class provides the foundational implementation for tree
///     items, including properties and methods for managing child items, parent references, and event
///     handling. It extends the <see cref="ObservableObject" /> class and implements the
///     <see cref="ITreeItem" /> interface.
///     <para>
///         The <see cref="Depth" /> property indicates the tree depth at which the item is located, and can
///         be only set from outside for the root item to hide it. In such case, the depth should be set to
///         `-1`. Otherwise, it will be automatically updated as the item gets placed under a parent or
///         removed from the tree.
///     </para>
///     <para>
///         When an item has a <see cref="Parent" />, its <see cref="Depth" /> will always be `1` more than
///         its parent, and every item has a <see cref="Parent" />, unless it is not currently in the tree,
///         or it is the <see cref="IsRoot">root</see> item.
///     </para>
///     <para>
///         The collection of the item's <see cref="Children" /> is lazily initialized on first access. This
///         helps in implementing gradually expanding trees, and reduce the load when the tree is first
///         displayed. Mark an item as <see cref="IsExpanded" /> to indicate that its <see cref="Children" />
///         collection should be populated when the item is displayed. This also applies to the
///         <see cref="ChildrenCount" /> property. It is lazily evaluated. If the <see cref="Children" />
///         collection has already been populated, the count is obtained from that collection; otherwise, it
///         is requested from the concrete class implementation via the <see cref="DoGetChildrenCount" />
///         abstract method.
///     </para>
/// </remarks>
public abstract partial class TreeItemAdapter : ObservableObject, ITreeItem
{
    /// <summary>
    ///     Represents the internal collection of child items in the tree. This collection is lazily initialized.
    /// </summary>
    /// <seealso cref="childrenLazy" />
    /// <seealso cref="InitializeChildrenCollectionAsync" />
    private readonly ObservableCollection<ITreeItem> children = [];

    /// <summary>
    ///     Lazily initialized read-only view of the collection of child items.
    /// </summary>
    private readonly Lazy<Task<ReadOnlyObservableCollection<ITreeItem>>> childrenLazy;

    private readonly bool isRoot;

    private bool suspendChildrenCollectionNotifications;

    /// <summary>
    ///     Initializes a new instance of the <see cref="TreeItemAdapter" /> class.
    /// </summary>
    /// <param name="isRoot">
    ///     Indicates if the item being created is a root item or not. Defaults to <see langword="false" />.
    /// </param>
    /// <param name="isHidden">
    ///     For a <paramref name="isRoot">root</paramref> item, indicates whether it should be hidden (its <see cref="Depth" />
    ///     set to
    ///     `-1`).
    /// </param>
    protected TreeItemAdapter(bool isRoot = false, bool isHidden = false)
    {
        this.IsRoot = isRoot;

        this.childrenLazy
            = new Lazy<Task<ReadOnlyObservableCollection<ITreeItem>>>(this.InitializeChildrenCollectionAsync);
        this.children.CollectionChanged += (_, args) => this.ChildrenCollectionChanged?.Invoke(this, args);

        this.Depth = isHidden ? -1 : 0;
    }

    /// <inheritdoc />
    public event EventHandler<NotifyCollectionChangedEventArgs>? ChildrenCollectionChanged;

    [ObservableProperty]
    public partial bool IsSelected { get; set; }

    [ObservableProperty]
    public partial bool IsExpanded { get; set; }

    /// <inheritdoc />
    public abstract string Label { get; set; }

    /// <inheritdoc />
    public bool IsRoot
    {
        get => this.isRoot;
        init
        {
            this.isRoot = value;
            this.IsLocked = value;
        }
    }

    /// <inheritdoc />
    public bool IsLocked
    {
        get;
        set
        {
            if (value == field)
            {
                return;
            }

            // Root item is always locked
            field = this.isRoot || value;
            this.OnPropertyChanged();
        }
    }

    /// <inheritdoc />
    public int Depth
    {
        get;
        private set
        {
            if (value == field)
            {
                return;
            }

            field = value;
            this.OnPropertyChanged();

            if (this.childrenLazy.IsValueCreated)
            {
                foreach (var child in this.children)
                {
                    if (child is TreeItemAdapter childAdapter)
                    {
                        childAdapter.Depth = field + 1;
                    }
                }
            }
        }
    }

    /// <inheritdoc />
    public ITreeItem? Parent
    {
        get
        {
            Debug.Assert(!this.IsRoot || field is null, "root item must always have a `null` Parent");
            return field;
        }

        private set
        {
            if (Equals(value, field))
            {
                return;
            }

            field = value;
            this.OnPropertyChanged();
            this.Depth = field is null ? int.MinValue : field.Depth + 1;
        }
    }

    /// <summary>
    ///     Gets the collection of child items.
    /// </summary>
    /// <remarks>
    ///     The collection of child items is lazily initialized. The first call to this property getter will trigger the
    ///     loading
    ///     of the collection items through the <seealso cref="LoadChildren" />.
    /// </remarks>
    public Task<ReadOnlyObservableCollection<ITreeItem>> Children => this.childrenLazy.Value;

    /// <inheritdoc />
    public int ChildrenCount => this.childrenLazy.IsValueCreated
        ? this.children.Count
        : this.DoGetChildrenCount();

    /// <inheritdoc />
    public abstract bool ValidateItemName(string name);

    /// <inheritdoc />
    /// <exception cref="ArgumentException">
    ///     Thrown when the item does not derive from <see cref="TreeItemAdapter" />.
    /// </exception>
    public async Task AddChildAsync(ITreeItem child)
    {
        this.CheckCanAddItem(child);

        await this.ManipulateChildrenAsync(this.AddChildInternal, child).ConfigureAwait(false);

        if (!this.suspendChildrenCollectionNotifications)
        {
            this.ChildrenCollectionChanged?.Invoke(
                this,
                new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Add, child, this.ChildrenCount - 1));
        }
    }

    /// <inheritdoc />
    /// <exception cref="ArgumentException">
    ///     Thrown when the item does not derive from <see cref="TreeItemAdapter" />.
    /// </exception>
    public async Task InsertChildAsync(int index, ITreeItem child)
    {
        this.CheckCanAddItem(child);

        await this.ManipulateChildrenAsync(
                (item) =>
                {
                    this.children.Insert(index, item);
                    item.Parent = this;
                },
                child)
            .ConfigureAwait(false);

        if (!this.suspendChildrenCollectionNotifications)
        {
            this.ChildrenCollectionChanged?.Invoke(
                this,
                new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Add, child, index));
        }
    }

    /// <inheritdoc />
    /// <exception cref="ArgumentException">
    ///     Thrown when the item does not derive from <see cref="TreeItemAdapter" />.
    /// </exception>
    public async Task<int> RemoveChildAsync(ITreeItem child)
    {
        var removeAtIndex = this.children.IndexOf(child);
        if (removeAtIndex == -1)
        {
            return -1;
        }

        await this.ManipulateChildrenAsync(
                (item) =>
                {
                    this.children.RemoveAt(removeAtIndex);
                    item.Parent = null;
                },
                child)
            .ConfigureAwait(false);

        if (!this.suspendChildrenCollectionNotifications)
        {
            this.ChildrenCollectionChanged?.Invoke(
                this,
                new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Remove, child, removeAtIndex));
        }

        return removeAtIndex;
    }

    /// <summary>
    ///     Gets the count of child tree items asynchronously.
    /// </summary>
    /// <returns>The count of child tree items.</returns>
    /// <remarks>
    ///     Due to the potential that getting the collection of all child items may be very expensive, it is suggested that
    ///     whenever
    ///     possible, getting the count of children items should be implemented in a lightweight and efficient manner that does
    ///     not
    ///     require a full update of the <seealso cref="Children" /> collection.
    /// </remarks>
    protected abstract int DoGetChildrenCount();

    /// <summary>
    ///     Loads the child items asynchronously. Used to lazily initialize the collection of child items.
    /// </summary>
    /// <returns>A task representing the asynchronous operation.</returns>
    /// <remarks>
    ///     Loading of child items on demand allows for reducing the workload when displaying the tree. Only expanded items
    ///     needs
    ///     their children to be loaded.
    /// </remarks>
    protected abstract Task LoadChildren();

    /// <summary>
    ///     Remove all child elements from the collection of children.
    /// </summary>
    protected void ClearChildren() => this.children.Clear();

    /// <summary>
    ///     Adds a child item to the children collection synchronously. Used internally, and by derived classes, to populate
    ///     the
    ///     collection when items are being added synchronously in batch. Use <see cref="AddChildAsync" /> for regular
    ///     operations to
    ///     add a child item.
    /// </summary>
    /// <param name="item">The child item to add.</param>
    protected void AddChildInternal(TreeItemAdapter item)
    {
        this.children.Add(item);
        item.Parent = this;
    }

    private void CheckCanAddItem(ITreeItem child)
    {
        if (child.Parent is not null || child.IsRoot)
        {
            throw new InvalidOperationException(
                "an item be added must not be the root item and must not have a parent");
        }

        if (child == this)
        {
            throw new InvalidOperationException("cannot ad self as a child");
        }
    }

    /// <summary>
    ///     Manipulates the child items asynchronously.
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
    ///     Initializes the collection of child items asynchronously.
    /// </summary>
    /// <returns>A task representing the asynchronous operation, holding the newly created children collection.</returns>
    private async Task<ReadOnlyObservableCollection<ITreeItem>> InitializeChildrenCollectionAsync()
    {
        Debug.Assert(
            this.children.Count == 0,
            "Ensure that the children collection is loaded before you add things to it");

        try
        {
            this.suspendChildrenCollectionNotifications = true;
            await this.LoadChildren().ConfigureAwait(false);

            this.ChildrenCollectionChanged?.Invoke(
                this,
                new NotifyCollectionChangedEventArgs(NotifyCollectionChangedAction.Reset));
        }
        finally
        {
            this.suspendChildrenCollectionNotifications = false;
        }

        return new ReadOnlyObservableCollection<ITreeItem>(this.children);
    }
}
