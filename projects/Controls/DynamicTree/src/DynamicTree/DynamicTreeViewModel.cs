// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Diagnostics;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using static DroidNet.Controls.TreeDisplayHelper;

namespace DroidNet.Controls;

/// <summary>
///     Represents the ViewModel for a dynamic tree control, providing functionality for managing
///     hierarchical data structures, including selection, expansion, and manipulation of tree items.
/// </summary>
/// <param name="loggerFactory">
///     The <see cref="ILoggerFactory" /> used to obtain an <see cref="ILogger" />. If the logger
///     cannot be obtained, a <see cref="NullLogger" /> is used silently.
/// </param>
/// <remarks>
///     This class provides the foundational implementation for a dynamic tree view model. It includes
///     methods for expanding and collapsing tree items, initializing the root item, inserting and
///     removing items, and managing selection within the tree.
///     <para>
///     To create a concrete implementation of this view model, derive from <see cref="DynamicTreeViewModel" />
///     and implement the necessary logic for your specific tree structure. Below is an example of how
///     to derive from this class and create a concrete view model and item adapter.</para>
/// </remarks>
public abstract partial class DynamicTreeViewModel(ILoggerFactory? loggerFactory = null) : ObservableObject
{
    private readonly ILogger logger = loggerFactory?.CreateLogger<DynamicTreeViewModel>() ?? NullLoggerFactory.Instance.CreateLogger<DynamicTreeViewModel>();
    private TreeDisplayHelper? displayHelper;

    /* TODO: need to make this private and expose a read only collection*/

    /// <summary>
    ///     Gets the collection of items currently shown in the tree.
    /// </summary>
    /// <remarks>
    ///     This collection is updated dynamically as items are expanded or collapsed. It is recommended
    ///     to expose this collection as a read-only collection in derived classes to prevent unintended
    ///     modifications.
    /// </remarks>
    public ObservableCollection<ITreeItem> ShownItems { get; } = [];

    /// <summary>
    ///     Gets the <see cref="ILoggerFactory"/> used to create loggers for this view model.
    /// </summary>
    public ILoggerFactory? LoggerFactory { get; } = loggerFactory;

    private TreeDisplayHelper DisplayHelper => this.displayHelper ??=
        new TreeDisplayHelper(
            this.ShownItems,
            () => this.SelectionModel,
            this.ExpandItemAsync,
            new TreeDisplayEventCallbacks(
                args =>
                {
                    this.ItemBeingAdded?.Invoke(this, args);
                    return args.Proceed;
                },
                args => this.ItemAdded?.Invoke(this, args),
                args =>
                {
                    this.ItemBeingRemoved?.Invoke(this, args);
                    return args.Proceed;
                },
                args => this.ItemRemoved?.Invoke(this, args),
                args =>
                {
                    this.ItemBeingMoved?.Invoke(this, args);
                    return args.Proceed;
                },
                args => this.ItemMoved?.Invoke(this, args)),
            this.logger);

    /// <summary>
    ///     Expands the specified tree item (which must be visible in the tree) asynchronously.
    /// </summary>
    /// <param name="itemAdapter">The tree item to expand.</param>
    /// <returns>A task representing the asynchronous operation.</returns>
    /// <exception cref="InvalidOperationException">Thrown when the item is not yet visible in the tree and cannot be expanded.</exception>
    public async Task ExpandItemAsync(ITreeItem itemAdapter)
    {
        if (itemAdapter.IsExpanded)
        {
            return;
        }

        if (!itemAdapter.IsRoot && itemAdapter.Parent?.IsExpanded != true)
        {
            // The item's parent has never loaded its children, or is collapsed.
            this.LogExpandItemNotVisible(itemAdapter);
            throw new InvalidOperationException("cannot expand item; its parent is not expanded");
        }

        this.LogExpandItem(itemAdapter);
        await this.RestoreExpandedChildrenAsync(itemAdapter).ConfigureAwait(true);
        itemAdapter.IsExpanded = true;
    }

    /// <summary>
    ///     Collapses the specified tree item asynchronously.
    /// </summary>
    /// <param name="itemAdapter">The tree item to collapse.</param>
    /// <returns>A task representing the asynchronous operation.</returns>
    /// <remarks>
    ///     If the item is already collapsed, this method does nothing.
    /// </remarks>
    public async Task CollapseItemAsync(ITreeItem itemAdapter)
    {
        if (!itemAdapter.IsExpanded)
        {
            return;
        }

        await this.HideChildrenAsync(itemAdapter).ConfigureAwait(true);
        itemAdapter.IsExpanded = false;
    }

    /// <summary>
    ///     Initializes the root item of the tree asynchronously.
    /// </summary>
    /// <param name="root">The root item to initialize.</param>
    /// <param name="skipRoot">
    ///     If <see langword="true" />, the root item itself is not added to the shown items collection,
    ///     only its children are added.
    /// </param>
    /// <returns>A task representing the asynchronous operation.</returns>
    public async Task InitializeRootAsync(ITreeItem root, bool skipRoot = true)
    {
        this.LogInitializeRoot(root, skipRoot);
        this.SelectionModel?.ClearSelection();
        this.LogShownItemsClear();
        this.ShownItems.Clear();

        if (!skipRoot)
        {
            this.LogShownItemsAdd(root);
            this.ShownItems.Add(root);
        }
        else
        {
            root.IsExpanded = true;
        }

        if (root.IsExpanded)
        {
            await this.RestoreExpandedChildrenAsync(root).ConfigureAwait(false);
        }

        this.SyncSelectionModelWithItems();
    }

    /// <summary>
    ///     Inserts a child item at the specified index under the given parent item asynchronously.
    /// </summary>
    /// <param name="relativeIndex">Zero-based index at which to insert under the parent.</param>
    /// <param name="parent">The parent that receives the child.</param>
    /// <param name="item">The child item to insert.</param>
    /// <returns>A task that completes when the insertion finishes.</returns>
    public async Task InsertItemAsync(int relativeIndex, ITreeItem parent, ITreeItem item)
    {
        await this.DisplayHelper.InsertItemAsync(relativeIndex, parent, item).ConfigureAwait(true);
    }

    /// <summary>
    ///     Removes the specified item and its children from the tree asynchronously.
    /// </summary>
    /// <param name="item">The item to remove.</param>
    /// <param name="updateSelection">Whether selection should be updated after removal.</param>
    /// <returns>A task representing the asynchronous operation.</returns>
    public async Task RemoveItemAsync(ITreeItem item, bool updateSelection = true)
    {
        await this.DisplayHelper.RemoveItemAsync(item, updateSelection).ConfigureAwait(true);
    }

    /// <summary>
    ///     Removes the currently selected items from the tree asynchronously.
    /// </summary>
    /// <returns>A task representing the asynchronous operation.</returns>
    /// <remarks>
    ///     This method supports both single- and multiple-selection models. If there is no
    ///     selection, the method returns immediately. Selection is cleared before modification so
    ///     that shown-item indices remain valid while items are removed. In single-selection mode,
    ///     the single selected item is removed if it is not locked. In multiple-selection mode, all
    ///     selected items that are not locked are removed; selection updates are deferred during
    ///     batch removes to avoid a burst of selection change events.
    /// </remarks>
    public virtual async Task RemoveSelectedItems()
        => await this.DisplayHelper.RemoveSelectedItemsAsync().ConfigureAwait(true);

    /// <summary>
    ///     Moves an item to a different parent and position within the tree asynchronously.
    /// </summary>
    /// <param name="item">The item to move.</param>
    /// <param name="newParent">The new parent under which the item is inserted.</param>
    /// <param name="newIndex">The zero-based index at which the item is inserted beneath the new parent.</param>
    /// <returns>A task that completes once the move finishes.</returns>
    public Task MoveItemAsync(ITreeItem item, ITreeItem newParent, int newIndex)
        => this.DisplayHelper.MoveItemAsync(item, newParent, newIndex);

    /// <summary>
    ///     Moves a batch of items into a new parent while preserving their relative order.
    /// </summary>
    /// <param name="items">The list of items to move.</param>
    /// <param name="newParent">The parent that receives the moved items.</param>
    /// <param name="startIndex">The index within <paramref name="newParent" /> where the first item is inserted.</param>
    /// <returns>A task that completes once the batch move finishes.</returns>
    public Task MoveItemsAsync(IReadOnlyList<ITreeItem> items, ITreeItem newParent, int startIndex)
        => this.DisplayHelper.MoveItemsAsync(items, newParent, startIndex);

    /// <summary>
    ///     Reorders a single item among its siblings asynchronously.
    /// </summary>
    /// <param name="item">The item whose shown position should change.</param>
    /// <param name="newIndex">The new zero-based index among the current parent&apos;s children.</param>
    /// <returns>A task that completes once the reorder finishes.</returns>
    public Task ReorderItemAsync(ITreeItem item, int newIndex)
        => this.DisplayHelper.ReorderItemAsync(item, newIndex);

    /// <summary>
    ///     Reorders a contiguous block of shown items beneath their current parent asynchronously.
    /// </summary>
    /// <param name="items">The ordered list of items to reposition.</param>
    /// <param name="startIndex">The destination index of the first item in <paramref name="items" />.</param>
    /// <returns>A task that completes once the block reorder finishes.</returns>
    public Task ReorderItemsAsync(IReadOnlyList<ITreeItem> items, int startIndex)
        => this.DisplayHelper.ReorderItemsAsync(items, startIndex);

    /// <summary>
    ///     Toggles the expansion state of the specified tree item asynchronously.
    /// </summary>
    /// <param name="itemAdapter">The tree item to toggle.</param>
    /// <returns>A task representing the asynchronous operation.</returns>
    /// <remarks>
    ///     If the item is currently expanded, it will be collapsed. If it is currently collapsed, it
    ///     will be expanded.
    /// </remarks>
    [RelayCommand]
    private async Task ToggleExpanded(TreeItemAdapter itemAdapter)
    {
        if (itemAdapter.IsExpanded)
        {
            await this.CollapseItemAsync(itemAdapter).ConfigureAwait(true);
        }
        else
        {
            await this.ExpandItemAsync(itemAdapter).ConfigureAwait(true);
        }
    }

    /// <summary>
    ///     Restores the expanded state of the children of the specified tree item asynchronously.
    /// </summary>
    /// <param name="itemAdapter">The tree item whose children should be restored.</param>
    /// <returns>A task representing the asynchronous operation.</returns>
    /// <remarks>
    ///     This method inserts the children of the specified item into the shown items collection,
    ///     maintaining their expanded state.
    /// </remarks>
    private async Task RestoreExpandedChildrenAsync(ITreeItem itemAdapter)
    {
        this.LogRestoreExpandedChildrenStarted(itemAdapter);
        var insertIndex = this.ShownItems.IndexOf(itemAdapter) + 1;
        _ = await this.RestoreExpandedChildrenRecursive(itemAdapter, insertIndex).ConfigureAwait(true);
    }

    /// <summary>
    ///     Recursively restores the expanded state of the children of the specified tree item.
    /// </summary>
    /// <param name="parent">The parent tree item whose children should be restored.</param>
    /// <param name="insertIndex">The index at which to start inserting the restored children in the ShownItems collection.</param>
    /// <returns>
    ///     A task representing the asynchronous operation, with the final insert index after all children have been
    ///     processed.
    /// </returns>
    /// <remarks>
    ///     This method inserts the children of the specified parent item into the ShownItems
    ///     collection, maintaining their expanded state. If a child item is expanded, the method is
    ///     called recursively to restore its children as well.
    /// </remarks>
    private async Task<int> RestoreExpandedChildrenRecursive(ITreeItem parent, int insertIndex)
    {
        foreach (var child in await parent.Children.ConfigureAwait(true))
        {
            this.LogShownItemsInsert(insertIndex, child);
            this.ShownItems.Insert(insertIndex, (TreeItemAdapter)child);
            ++insertIndex;

            if (child.IsExpanded)
            {
                insertIndex = await this.RestoreExpandedChildrenRecursive(child, insertIndex).ConfigureAwait(true);
            }
        }

        return insertIndex;
    }

    /// <summary>
    ///     Hides the children of the specified tree item asynchronously.
    /// </summary>
    /// <param name="itemAdapter">The tree item whose children should be hidden.</param>
    /// <returns>A task representing the asynchronous operation.</returns>
    /// <remarks>
    ///     This method removes the children of the specified item from the shown items collection,
    ///     maintaining their collapsed state.
    /// </remarks>
    private async Task HideChildrenAsync(ITreeItem itemAdapter)
    {
        this.LogHideChildrenStarted(itemAdapter);
        var removeIndex = this.ShownItems.IndexOf((TreeItemAdapter)itemAdapter) + 1;
        Debug.Assert(removeIndex != -1, $"expecting item {itemAdapter.Label} to be in the shown list");

        await this.HideChildrenRecursiveAsync(itemAdapter, removeIndex).ConfigureAwait(true);
    }

    /// <summary>
    ///     Recursively hides the children of the specified tree item.
    /// </summary>
    /// <param name="parent">The parent tree item whose children should be hidden.</param>
    /// <param name="removeIndex">The index at which to start removing the hidden children from the ShownItems collection.</param>
    /// <returns>A task representing the asynchronous operation.</returns>
    /// <remarks>
    ///     This method removes the children of the specified parent item from the ShownItems collection,
    ///     maintaining their collapsed state. If a child item is expanded, the method is called recursively
    ///     to hide its children as well.
    /// </remarks>
    private async Task HideChildrenRecursiveAsync(ITreeItem parent, int removeIndex)
    {
        foreach (var child in await parent.Children.ConfigureAwait(true))
        {
            this.LogShownItemsRemoveAt(removeIndex);
            this.ShownItems.RemoveAt(removeIndex);
            if (child.IsExpanded)
            {
                await this.HideChildrenRecursiveAsync(child, removeIndex).ConfigureAwait(true);
            }
        }
    }
}
