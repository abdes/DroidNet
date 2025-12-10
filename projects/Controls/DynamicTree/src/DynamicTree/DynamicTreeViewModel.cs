// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Diagnostics;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

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
    ///     only its children are added. Defaults to <see langword="true" />.
    /// </param>
    /// <returns>A task representing the asynchronous operation.</returns>
    /// <remarks>
    ///     This method clears the current shown items and initializes the tree starting from the root
    ///     item. If the root item is expanded, its children will be restored as well.
    /// </remarks>
    protected async Task InitializeRootAsync(ITreeItem root, bool skipRoot = true)
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
            // Do not add the root item, add its children instead, and check if they need to be expanded
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
    /// <param name="relativeIndex">The zero-based index at which the child item should be inserted.</param>
    /// <param name="parent">The parent item under which the child item should be inserted.</param>
    /// <param name="item">The child item to insert.</param>
    /// <returns>A task representing the asynchronous operation.</returns>
    /// <remarks>
    ///     This method ensures that the parent item is expanded before inserting the child item. It
    ///     also fires the <see cref="ItemBeingAdded" /> event before making any changes to the tree,
    ///     allowing the operation to be aborted if necessary.
    /// </remarks>
    protected async Task InsertItemAsync(int relativeIndex, ITreeItem parent, ITreeItem item)
    {
        this.LogInsertItemRequested(parent, relativeIndex, item);

        relativeIndex = ValidateRelativeIndex();

        // Always expand the item before adding to force the children collection to be fully loaded.
        await EnsureParentIsExpandedAsync().ConfigureAwait(true);

        // Fire the ItemBeingAdded event before any changes are made to the tree, and stop if the
        // returned verdict is not to proceed
        if (!ApproveItemBeingAdded())
        {
            return;
        }

        // Clear selection first because after insertion, the selected indices will be invalid
        this.SelectionModel?.ClearSelection();

        // Determine where the item should be inserted in the shown items collection
        // NOTE: do this before the items is added to its parent children collection
        var newItemIndex = await FindNewItemIndexAsync().ConfigureAwait(true);

        // Add the item to its parent first then to the shown items collection
        await parent.InsertChildAsync(relativeIndex, item).ConfigureAwait(true);
        this.LogShownItemsInsert(newItemIndex, item);
        this.ShownItems.Insert(newItemIndex, item);

        // Select the new item
        this.SelectionModel?.SelectItemAt(newItemIndex);

        FireItemAddedEvent();
        return;

        /*
         * Validates the relative index to ensure it is within the valid range.
         * @returns The validated relative index.
         */
        int ValidateRelativeIndex()
        {
            var originalIndex = relativeIndex;
            relativeIndex = Math.Clamp(relativeIndex, 0, parent.ChildrenCount);
            this.LogRelativeIndexAdjusted(parent, originalIndex, relativeIndex);
            return relativeIndex;
        }

        /*
         * Ensures that the parent item is expanded before inserting the child item.
         * @returns A task representing the asynchronous operation.
         */
        async Task EnsureParentIsExpandedAsync()
        {
            if (!parent.IsExpanded)
            {
                this.LogParentAutoExpanded(parent);
                await this.ExpandItemAsync(parent).ConfigureAwait(true);
            }
        }

        /*
         * Fires the ItemBeingAdded event and returns whether the addition should proceed.
         * @returns true if the addition should proceed; otherwise, false.
         */
        bool ApproveItemBeingAdded()
        {
            var eventArgs = new TreeItemBeingAddedEventArgs { TreeItem = item, Parent = parent };
            this.ItemBeingAdded?.Invoke(this, eventArgs);
            this.LogItemBeingAddedDecision(parent, item, eventArgs.Proceed);
            return eventArgs.Proceed;
        }

        /*
         * Finds the new index for the item in the shown items collection.
         * @returns The new index for the item.
         *
         * This function determines the appropriate index in the ShownItems collection where the new
         * item should be inserted, respecting the display hierarchy (indentation) of the items in
         * the tree.
         *
         * The logic for determining the index is as follows:
         *
         * 1. If the relative index is 0, the new item is the first child of the parent. The index
         *    is set to one position after the parent in the ShownItems collection.
         * 2. If the relative index is the last position among the parent's children, the new item
         *    is the last child. The index is set to just after the previous sibling in the ShownItems
         *    collection.
         * 3. For any other relative index, the new item is inserted between existing siblings. The
         *    index is set to just before the next sibling in the ShownItems collection.
         */
        async Task<int> FindNewItemIndexAsync()
        {
            int i;
            var siblingRole = "none";
            ITreeItem? sibling = null;
            if (relativeIndex == 0)
            {
                i = this.ShownItems.IndexOf(parent) + 1;
            }
            else if (relativeIndex == parent.ChildrenCount)
            {
                // Find the previous sibling index in the shown items collection and use
                // it to insert the item just after it.
                var siblings = await parent.Children.ConfigureAwait(true);
                sibling = siblings[relativeIndex - 1];
                siblingRole = "prev";
                var siblingFound = false;
                i = 0;
                foreach (var shownItem in this.ShownItems)
                {
                    if (!siblingFound && shownItem != sibling)
                    {
                        ++i;
                        continue;
                    }

                    siblingFound = true;

                    if (shownItem.Depth < sibling.Depth)
                    {
                        break;
                    }

                    ++i;
                }

                Debug.Assert(i >= 0, "must be a valid index");
            }
            else
            {
                // Find the next sibling index in the shown items collection and use
                // it to insert the item just before it.
                var siblings = await parent.Children.ConfigureAwait(true);
                sibling = siblings[relativeIndex];
                siblingRole = "next";
                i = this.ShownItems.IndexOf(sibling);
                Debug.Assert(i >= 0, "must be a valid index");
            }

            this.LogFindNewItemIndexComputed(parent, relativeIndex, i, siblingRole, sibling);
            return i;
        }

        /*
         * Fires the ItemAdded event after the item has been added to the tree.
         */
        void FireItemAddedEvent()
        {
            this.ItemAdded?.Invoke(
                this,
                new TreeItemAddedEventArgs { Parent = parent, TreeItem = item, RelativeIndex = relativeIndex });
        }
    }

    /* TODO: should add events for item add, delete, move to different parent */

    /// <summary>
    ///     Removes the specified item and its children from the tree asynchronously.
    /// </summary>
    /// <param name="item">The item to remove.</param>
    /// <param name="updateSelection">
    ///     If <see langword="true" />, the selection is updated after the item is removed. Defaults to
    ///     <see langword="true" />. Set it to <see langword="false" /> if multiple items are being removed
    ///     in batch to avoid a burst of selection change events.
    /// </param>
    /// <returns>A task representing the asynchronous operation.</returns>
    /// <remarks>
    ///     This method fires the <see cref="ItemBeingRemoved" /> event before making any changes to the
    ///     tree, allowing the operation to be aborted if necessary. It also removes the item's children
    ///     from the shown items collection in a single pass.
    /// </remarks>
    protected async Task RemoveItemAsync(ITreeItem item, bool updateSelection = true)
    {
        // Capture how many items are shown so we can log how many visible items were removed
        this.LogRemoveItemRequested(item);

        var (itemIsShown, removeAtIndex) = EnsureCanRemove();

        if (!ApproveItemBeingRemoved())
        {
            return;
        }

        if (itemIsShown && updateSelection)
        {
            this.SelectionModel?.ClearSelection();
        }

        var shownItemsBeforeRemoval = this.ShownItems.Count;

        // Remove the item's children from their parent and from the ShownItems in a single pass
        await this.RemoveChildren(item, itemIsShown, removeAtIndex).ConfigureAwait(true);

        // Remove the item from its parent
        var itemParent = item.Parent;
        Debug.Assert(itemParent != null, "an item being removed from the tree always has a parent");
        var itemIndex = await itemParent.RemoveChildAsync(item).ConfigureAwait(true);
        Debug.Assert(itemIndex != -1, "the item should exist as it is part of the selection");

        // Remove the item from the ShownItems if it was visible
        if (itemIsShown)
        {
            this.LogShownItemsRemoveAt(removeAtIndex);
            this.ShownItems.RemoveAt(removeAtIndex);
        }

        if (itemIsShown && updateSelection)
        {
            this.UpdateSelectionAfterRemoval(removeAtIndex, 1);
        }

        this.ItemRemoved?.Invoke(
            this,
            new TreeItemRemovedEventArgs { RelativeIndex = itemIndex, TreeItem = item, Parent = itemParent });

        var removedShownItemsCount = shownItemsBeforeRemoval - this.ShownItems.Count;
        this.LogRemoveItemCompleted(item, removedShownItemsCount, itemIsShown);

        /*
         * Ensures the item can be removed; throws if it cannot be removed. Returns whether the
         * item is currently shown and its index in the ShownItems list (or -1 if not shown).
         */
        (bool isShown, int atIndex) EnsureCanRemove()
        {
            if (item.IsLocked)
            {
                this.LogErrorRemoveLockedItem(item);
                throw new InvalidOperationException($"attempt to remove locked item `{item.Label}`");
            }

            var shown = true;
            var index = this.ShownItems.IndexOf(item);
            if (index == -1)
            {
                shown = false;

                // The item is not shown yet, which either means it's inside a collapsed branch or it's
                // an invalid orphan item
                if (item is { IsRoot: false, Parent: null })
                {
                    this.LogErrorRemoveOrphanItem(item);
                    throw new InvalidOperationException($"attempt to remove orphan item `{item.Label}`");
                }
            }

            return (shown, index);
        }

        bool ApproveItemBeingRemoved()
        {
            var eventArgs = new TreeItemBeingRemovedEventArgs { TreeItem = item };
            this.ItemBeingRemoved?.Invoke(this, eventArgs);
            this.LogItemBeingRemovedDecision(item, eventArgs.Proceed);
            return eventArgs.Proceed;
        }
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
    protected virtual async Task RemoveSelectedItems()
    {
        if (this.SelectionModel?.IsEmpty ?? false)
        {
            return;
        }

        if (this.SelectionModel is SingleSelectionModel)
        {
            await this.RemoveSingleSelectedItem().ConfigureAwait(true);
        }

        if (this.SelectionModel is MultipleSelectionModel)
        {
            await this.RemoveMultipleSelectionItems().ConfigureAwait(true);
        }
    }

    private async Task RemoveSingleSelectedItem()
    {
        var singleSelection = this.SelectionModel as SingleSelectionModel;
        Debug.Assert(
            singleSelection is not null,
            $"{nameof(this.RemoveSingleSelectedItem)} should only be used in single selection mode");

        var selectedItem = singleSelection.SelectedItem;
        this.LogRemoveSingleSelectedItem(selectedItem!);

        if (selectedItem?.IsLocked == false)
        {
            singleSelection.ClearSelection();

            await this.RemoveItemAsync(selectedItem).ConfigureAwait(true);
        }
    }

    private async Task RemoveMultipleSelectionItems()
    {
        var multipleSelection = this.SelectionModel as MultipleSelectionModel;
        Debug.Assert(
            multipleSelection is not null,
            $"{nameof(this.RemoveMultipleSelectionItems)} should only be used in multiple selection mode");

        // Save the selection in a list for processing the removal
        var selectedIndices = multipleSelection.SelectedIndices.OrderDescending().ToList();
        this.LogRemoveMultipleSelectionItemsStarted(selectedIndices.Count);

        // Clear the selection before we start modifying the shown items collection so that the
        // indices are still valid while updating the items being deselected.
        multipleSelection.ClearSelection();

        var originalShownItemsCount = this.ShownItems.Count;

        var tasks = selectedIndices
            .Select(index => this.ShownItems[index])
            .Where(item => !item.IsLocked)
            .Aggregate(
                new List<ITreeItem>(),
                (list, item) =>
                {
                    list.Add(item);
                    return list;
                })
            .Select(async item => await this.RemoveItemAsync(item, updateSelection: false).ConfigureAwait(true));

        await Task.WhenAll(tasks).ConfigureAwait(true);

        var removedItemsCount = originalShownItemsCount - this.ShownItems.Count;
        this.LogRemoveMultipleSelectionItemsCompleted(removedItemsCount);

        this.UpdateSelectionAfterRemoval(selectedIndices[0], removedItemsCount);
    }

    /// <summary>
    ///     Removes the children of the specified item from the item and from the <see cref="ShownItems" /> if the item is
    ///     expanded.
    /// </summary>
    /// <param name="item">The item whose children should be removed.</param>
    /// <param name="itemIsShown">Indicates whether the item is currently shown in the tree.</param>
    /// <param name="atIndex">The index at which the item is located in the ShownItems collection.</param>
    /// <returns>A task representing the asynchronous operation.</returns>
    private async Task RemoveChildren(ITreeItem item, bool itemIsShown, int atIndex)
    {
        this.LogRemoveChildrenStarted(item.ChildrenCount, itemIsShown, item.IsExpanded);

        foreach (var child in (await item.Children.ConfigureAwait(true)).Reverse())
        {
            var childIndex = await item.RemoveChildAsync(child).ConfigureAwait(true);
            if (itemIsShown && item.IsExpanded)
            {
                this.LogShownItemsRemoveAt(atIndex + childIndex + 1);
                this.ShownItems.RemoveAt(atIndex + childIndex + 1);
            }

            this.ItemRemoved?.Invoke(
                this,
                new TreeItemRemovedEventArgs { RelativeIndex = childIndex, TreeItem = child, Parent = item });
        }
    }

    /// <summary>
    ///     Updates the selection after items have been removed from the tree.
    /// </summary>
    /// <param name="lastSelectedIndex">The index of the last selected item before removal.</param>
    /// <param name="removedItemsCount">The number of items that were removed.</param>
    /// <remarks>
    ///     This method adjusts the selection to ensure that a valid item remains selected after items
    ///     have been removed from the tree. In single selection mode, if the last selected index is
    ///     still within the bounds of the updated ShownItems collection, it will be used as the new
    ///     selected index; otherwise, the selection defaults to the first item in the ShownItems
    ///     collection if it is not empty.
    ///     <para>
    ///         In multiple selection mode, the method attempts to select the next item after the last
    ///         selected index, adjusted for the number of removed items; if this index is valid, it will be
    ///         used as the new selected index, otherwise, the selection defaults to the first item in the
    ///         ShownItems collection if it is not empty.
    ///     </para>
    /// </remarks>
    private void UpdateSelectionAfterRemoval(int lastSelectedIndex, int removedItemsCount)
    {
        var newSelectedIndex = this.ShownItems.Count != 0 ? 0 : -1;
        switch (this.SelectionModel)
        {
            case SingleSelectionModel:
                if (lastSelectedIndex < this.ShownItems.Count)
                {
                    newSelectedIndex = lastSelectedIndex;
                }

                break;

            case MultipleSelectionModel:
                // Select the next item after the last index in selectedIndices
                // if it's valid, otherwise, select the first item if the items
                // collection is not empty, otherwise leave the selection empty.
                var tentativeIndex = lastSelectedIndex + 1 - removedItemsCount;
                if (tentativeIndex > 0 && tentativeIndex < this.ShownItems.Count)
                {
                    newSelectedIndex = tentativeIndex;
                }

                break;

            default:
                return;
        }

        if (newSelectedIndex == -1)
        {
            return;
        }

        this.LogUpdateSelectionAfterRemoval(newSelectedIndex, lastSelectedIndex, removedItemsCount);
        this.SelectionModel.SelectItemAt(newSelectedIndex);
    }

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
