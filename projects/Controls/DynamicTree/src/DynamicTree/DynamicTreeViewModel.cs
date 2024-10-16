// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

using System.Collections.ObjectModel;
using System.Diagnostics;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

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

    protected async Task InitializeRootAsync(ITreeItem root)
    {
        this.ShownItems.Clear();

        // Do not add the root item, add its children instead and check if it they need to be expandedxx
        root.IsExpanded = true;
        await this.RestoreExpandedChildrenAsync(root).ConfigureAwait(true);
    }

    protected async Task AddItem(ITreeItem parent, ITreeItem item)
    {
        // Always expand the item before adding to to force the children collection to be fully loaded.
        if (!parent.IsExpanded)
        {
            await this.ExpandItemAsync(parent).ConfigureAwait(true);
        }

        // Fire the ItemBeingAdded event before any changes are made to the tree
        var eventArgs = new ItemBeingAddedEventArgs()
        {
            TreeItem = item,
            Parent = parent,
        };
        this.ItemBeingAdded?.Invoke(this, eventArgs);
        if (!eventArgs.Proceed)
        {
            return;
        }

        // Clear selection first because after insertion, the selected indices will be invalid
        this.SelectionModel?.ClearSelection();

        await parent.InsertChildAsync(0, item).ConfigureAwait(true);
        var newItemIndex = this.ShownItems.IndexOf(parent) + 1;
        this.ShownItems.Insert(newItemIndex, item);

        this.SelectionModel?.SelectItemAt(newItemIndex);

        this.ItemAdded?.Invoke(this, new ItemAddedEventArgs() { TreeItem = item });
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
        var eventArgs = new ItemBeingRemovedEventArgs() { TreeItem = item };
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
        var children = (await item.Children.ConfigureAwait(true)).ToArray();
        for (var childIndex = children.Length - 1; childIndex >= 0; childIndex--)
        {
            var child = children[childIndex];
            await item.RemoveChildAsync(child).ConfigureAwait(true);
            if (item.IsExpanded)
            {
                this.ShownItems.RemoveAt(removeAtIndex + childIndex + 1);
            }

            this.ItemRemoved?.Invoke(
                this,
                new ItemRemovedEventArgs()
                {
                    TreeItem = child,
                    Parent = item,
                });
        }

        // Remove the item
        var itemParent = item.Parent;
        Debug.Assert(itemParent != null, "an item being removed from the tree always has a parent");
        await itemParent.RemoveChildAsync(item).ConfigureAwait(true);
        this.ShownItems.RemoveAt(removeAtIndex);

        if (updateSelection)
        {
            this.UpdateSelectionAfterRemoval(removeAtIndex, 1);
        }

        this.ItemRemoved?.Invoke(
            this,
            new ItemRemovedEventArgs()
            {
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
            var selectedIndex = this.SelectionModel.SelectedIndex;
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
