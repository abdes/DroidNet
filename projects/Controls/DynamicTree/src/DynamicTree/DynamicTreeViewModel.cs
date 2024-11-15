// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

// TODO: item delete and add still puts items in the wrong place

/// <summary>
/// A ViewModel for the <see cref="DynamicTree" /> control.
/// </summary>
public abstract partial class DynamicTreeViewModel : ObservableObject
{
    // TODO: need to make this private and expose a read only collection
    public ObservableCollection<ITreeItem> ShownItems { get; } = [];

    public async Task ExpandItemAsync(ITreeItem itemAdapter)
    {
        if (itemAdapter.IsExpanded)
        {
            return;
        }

        if (itemAdapter.Parent?.IsExpanded == false)
        {
            await this.ExpandItemAsync(itemAdapter.Parent).ConfigureAwait(true);
        }

        await this.RestoreExpandedChildrenAsync(itemAdapter).ConfigureAwait(true);
        itemAdapter.IsExpanded = true;
    }

    public async Task CollapseItemAsync(ITreeItem itemAdapter)
    {
        if (!itemAdapter.IsExpanded)
        {
            return;
        }

        await this.HideChildrenAsync(itemAdapter).ConfigureAwait(true);
        itemAdapter.IsExpanded = false;
    }

    protected async Task InitializeRootAsync(ITreeItem root, bool skipRoot = true)
    {
        this.ShownItems.Clear();

        if (!skipRoot)
        {
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
    }

    protected async Task InsertItem(int relativeIndex, ITreeItem parent, ITreeItem item)
    {
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

        // Add new item to its parent
        await parent.InsertChildAsync(relativeIndex, item).ConfigureAwait(true);

        // Update our shown items collection
        var newItemIndex = await FindNewItemIndexAsync().ConfigureAwait(true);
        this.ShownItems.Insert(newItemIndex, item);

        // Select the new item
        this.SelectionModel?.SelectItemAt(newItemIndex);

        FireItemAddedEvent();
        return;

        int ValidateRelativeIndex()
        {
            if (relativeIndex < 0)
            {
                Debug.WriteLine(
                    $"Request to insert an item in the tree with negative index: {relativeIndex.ToString(CultureInfo.InvariantCulture)}");
                relativeIndex = 0;
            }

            if (relativeIndex > parent.ChildrenCount)
            {
                Debug.WriteLine(
                    $"Request to insert an item in the tree at index: {relativeIndex.ToString(CultureInfo.InvariantCulture)}, " +
                    $"which is out of range [0, {parent.ChildrenCount.ToString(CultureInfo.InvariantCulture)}]");
                relativeIndex = parent.ChildrenCount;
            }

            return relativeIndex;
        }

        async Task EnsureParentIsExpandedAsync()
        {
            if (!parent.IsExpanded)
            {
                await this.ExpandItemAsync(parent).ConfigureAwait(true);
            }
        }

        bool ApproveItemBeingAdded()
        {
            var eventArgs = new TreeItemBeingAddedEventArgs()
            {
                TreeItem = item,
                Parent = parent,
            };
            this.ItemBeingAdded?.Invoke(this, eventArgs);
            return eventArgs.Proceed;
        }

        async Task<int> FindNewItemIndexAsync()
        {
            int i;
            if (relativeIndex == 0)
            {
                i = this.ShownItems.IndexOf(parent) + 1;
            }
            else if (relativeIndex == parent.ChildrenCount - 1)
            {
                // Find the previous sibling index in the shown items collection and use
                // it to insert the item just after it.
                var siblings = await parent.Children.ConfigureAwait(true);
                var siblingBefore = siblings[relativeIndex - 1];
                var siblingFound = false;
                i = 0;
                foreach (var shownItem in this.ShownItems)
                {
                    if (!siblingFound && shownItem != siblingBefore)
                    {
                        ++i;
                        continue;
                    }

                    siblingFound = true;

                    if (shownItem.Depth < siblingBefore.Depth)
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
                var nextSibling = siblings[relativeIndex + 1];
                i = this.ShownItems.IndexOf(nextSibling);
                Debug.Assert(i >= 0, "must be a valid index");
            }

            return i;
        }

        void FireItemAddedEvent()
            => this.ItemAdded?.Invoke(
                this,
                new TreeItemAddedEventArgs
                {
                    RelativeIndex = relativeIndex,
                    TreeItem = item,
                });
    }

    // TODO: should add events for item add, delete, move to different parent
    protected async Task RemoveItem(ITreeItem item, bool updateSelection = true)
    {
        var removeAtIndex = this.ShownItems.IndexOf(item);
        if (removeAtIndex == -1)
        {
            Debug.WriteLine($"Expecting item ({item.Label}) to be in the shown items collection but it was not");
            return;
        }

        // Fire the ItemBeingRemoved event before any changes are made to the tree
        var eventArgs = new TreeItemBeingRemovedEventArgs() { TreeItem = item };
        this.ItemBeingRemoved?.Invoke(this, eventArgs);
        if (!eventArgs.Proceed)
        {
            return;
        }

        if (updateSelection)
        {
            this.SelectionModel?.ClearSelection();
        }

        // Remove the item's children from ShownItems in a single pass
        foreach (var child in (await item.Children.ConfigureAwait(true)).Reverse())
        {
            var childIndex = await item.RemoveChildAsync(child).ConfigureAwait(true);
            if (item.IsExpanded)
            {
                this.ShownItems.RemoveAt(removeAtIndex + childIndex + 1);
            }

            this.ItemRemoved?.Invoke(
                this,
                new TreeItemRemovedEventArgs()
                {
                    RelativeIndex = childIndex,
                    TreeItem = child,
                    Parent = item,
                });
        }

        // Remove the item
        var itemParent = item.Parent;
        Debug.Assert(itemParent != null, "an item being removed from the tree always has a parent");
        var itemIndex = await itemParent.RemoveChildAsync(item).ConfigureAwait(true);
        Debug.Assert(itemIndex != -1, "the item should exist as it is part of the selection");
        this.ShownItems.RemoveAt(removeAtIndex);

        if (updateSelection)
        {
            this.UpdateSelectionAfterRemoval(removeAtIndex, 1);
        }

        this.ItemRemoved?.Invoke(
            this,
            new TreeItemRemovedEventArgs()
            {
                RelativeIndex = itemIndex,
                TreeItem = item,
                Parent = itemParent,
            });
    }

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
        if (this.SelectionModel is not SingleSelectionModel singleSelection)
        {
            throw new InvalidOperationException(
                $"{nameof(this.RemoveSingleSelectedItem)} should only be used in single selection mode");
        }

        var selectedItem = singleSelection.SelectedItem;
        if (selectedItem?.IsLocked == false)
        {
            this.SelectionModel.ClearSelection();

            await this.RemoveItem(selectedItem).ConfigureAwait(true);
        }
    }

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
                // Select the next item after the last index in selectedIndices if it's valid, otherwise, select the first
                // item if the items collection is not empty, other wise leave the selection empty.
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

        this.SelectionModel.SelectItemAt(newSelectedIndex);
    }

    private async Task RemoveMultipleSelectionItems()
    {
        if (this.SelectionModel is not MultipleSelectionModel multipleSelection)
        {
            throw new InvalidOperationException(
                $"{nameof(this.RemoveSingleSelectedItem)} should only be used in multiple selection mode");
        }

        // Save the selection in a list for processing the removal
        var selectedIndices = multipleSelection.SelectedIndices.OrderDescending().ToList();

        // Clear the selection before we start modifying the shown items
        // collection so that the indices are still valid while updating the
        // items being deselected.
        this.SelectionModel.ClearSelection();

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
            .Select(async item => await this.RemoveItem(item, updateSelection: false).ConfigureAwait(true));

        await Task.WhenAll(tasks).ConfigureAwait(true);

        var removedItemsCount = originalShownItemsCount - this.ShownItems.Count;

        this.UpdateSelectionAfterRemoval(selectedIndices[0], removedItemsCount);
    }

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

    private async Task RestoreExpandedChildrenAsync(ITreeItem itemAdapter)
    {
        var insertIndex = this.ShownItems.IndexOf(itemAdapter) + 1;
        await this.RestoreExpandedChildrenRecursive(itemAdapter, insertIndex).ConfigureAwait(true);
    }

    private async Task<int> RestoreExpandedChildrenRecursive(ITreeItem parent, int insertIndex)
    {
        foreach (var child in await parent.Children.ConfigureAwait(true))
        {
            this.ShownItems.Insert(insertIndex++, (TreeItemAdapter)child);
            if (child.IsExpanded)
            {
                insertIndex = await this.RestoreExpandedChildrenRecursive(child, insertIndex).ConfigureAwait(true);
            }
        }

        return insertIndex;
    }

    private async Task HideChildrenAsync(ITreeItem itemAdapter)
    {
        var removeIndex = this.ShownItems.IndexOf((TreeItemAdapter)itemAdapter) + 1;
        Debug.Assert(removeIndex != -1, $"expecting item {itemAdapter.Label} to be in the shown list");

        await this.HideChildrenRecursiveAsync(itemAdapter, removeIndex).ConfigureAwait(true);
    }

    private async Task HideChildrenRecursiveAsync(ITreeItem parent, int removeIndex)
    {
        foreach (var child in await parent.Children.ConfigureAwait(true))
        {
            this.ShownItems.RemoveAt(removeIndex);
            if (child.IsExpanded)
            {
                await this.HideChildrenRecursiveAsync(child, removeIndex).ConfigureAwait(true);
            }
        }
    }
}
