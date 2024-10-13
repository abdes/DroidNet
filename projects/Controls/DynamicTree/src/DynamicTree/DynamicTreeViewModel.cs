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

        await this.RestoreExpandedChildrenAsync(itemAdapter).ConfigureAwait(false);
        itemAdapter.IsExpanded = true;
    }

    public async Task CollapseItemAsync(ITreeItem itemAdapter)
    {
        if (!itemAdapter.IsExpanded)
        {
            return;
        }

        await this.HideChildrenAsync(itemAdapter).ConfigureAwait(false);
        itemAdapter.IsExpanded = false;
    }

    protected async Task InitializeRootAsync(ITreeItem root)
    {
        this.ShownItems.Clear();

        // Do not add the root item, add its children instead and check if it they need to be expandedxx
        await this.RestoreExpandedChildrenAsync(root).ConfigureAwait(false);
    }

    protected async Task AddItem(ITreeItem parent, ITreeItem item)
    {
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

        await parent.InsertChildAsync(0, item).ConfigureAwait(false);
        var newItemIndex = this.ShownItems.IndexOf(parent) + 1;
        this.ShownItems.Insert(newItemIndex, item);
        this.SelectionModel?.SelectItemAt(newItemIndex);

        this.ItemAdded?.Invoke(this, new ItemAddedEventArgs() { TreeItem = item });
    }

    // TODO: should add events for item add, delete, move to different parent
    private async Task RemoveItem(ITreeItem item)
    {
        // Fire the ItemBeingRemoved event before any changes are made to the tree
        var eventArgs = new ItemBeingRemovedEventArgs() { TreeItem = item };
        this.ItemBeingRemoved?.Invoke(this, eventArgs);
        if (!eventArgs.Proceed)
        {
            return;
        }

        if (item.Parent != null)
        {
            await item.Parent.RemoveChildAsync(item).ConfigureAwait(false);
        }

        // Remove the item and its children from ShownItems in a single pass
        for (var removeAtIndex = this.ShownItems.IndexOf(item); removeAtIndex < this.ShownItems.Count;)
        {
            if (this.ShownItems[removeAtIndex] == item || this.ShownItems[removeAtIndex].Parent == item)
            {
                this.ShownItems.RemoveAt(removeAtIndex);
            }
            else
            {
                break;
            }
        }

        this.ItemRemoved?.Invoke(this, new ItemRemovedEventArgs() { TreeItem = item });
    }

    [RelayCommand(CanExecute = nameof(HasUnlockedSelectedItems))]
    private async Task RemoveSelectedItems()
    {
        if (this.SelectionModel?.IsEmpty ?? false)
        {
            return;
        }

        if (this.SelectionModel is SingleSelectionModel singleSelection)
        {
            var selectedItem = singleSelection.SelectedItem;
            if (selectedItem is not null /* TODO: add possibility to protect item against delete */)
            {
                this.SelectionModel.ClearSelection();
                await this.RemoveItem(selectedItem).ConfigureAwait(false);
            }
        }

        if (this.SelectionModel is MultipleSelectionModel multipleSelection)
        {
            // Save the selection in a list for processing the removal
            var selectedIndices = multipleSelection.SelectedIndices.ToList();

            // Clear the selection before we start modifying the shown items
            // collection so that the indices are still valid while updating the
            // items being deselected.
            this.SelectionModel.ClearSelection();

            var tasks = selectedIndices
                .Order()
                .Select(index => this.ShownItems[index])
                .Where(item => !item.IsLocked)
                .Aggregate(
                    new Stack<ITreeItem>(),
                    (stack, item) =>
                    {
                        if (item.Parent == null || !stack.Contains(item.Parent))
                        {
                            stack.Push(item);
                        }

                        return stack;
                    })
                .Select(async item => await this.RemoveItem(item).ConfigureAwait(false));

            await Task.WhenAll(tasks).ConfigureAwait(false);

            // Select the next item after the last index in selectedIndices if it's valid, otherwise, select the first
            // item if the items collection is not empty, other wise leave the selection empty.
            var newSelectedIndex = selectedIndices[^1] + 1 - selectedIndices.Count;
            if (newSelectedIndex < this.ShownItems.Count)
            {
                this.SelectionModel.SelectItemAt(newSelectedIndex);
            }
            else if (this.ShownItems.Count > 0)
            {
                this.SelectionModel.SelectItemAt(0);
            }
        }
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
        await this.RestoreExpandedChildrenRecursive(itemAdapter, insertIndex).ConfigureAwait(false);
    }

    private async Task<int> RestoreExpandedChildrenRecursive(ITreeItem parent, int insertIndex)
    {
        foreach (var child in await parent.Children.ConfigureAwait(false))
        {
            this.ShownItems.Insert(insertIndex++, (TreeItemAdapter)child);
            if (child.IsExpanded)
            {
                insertIndex = await this.RestoreExpandedChildrenRecursive(child, insertIndex).ConfigureAwait(false);
            }
        }

        return insertIndex;
    }

    private async Task HideChildrenAsync(ITreeItem itemAdapter)
    {
        var removeIndex = this.ShownItems.IndexOf((TreeItemAdapter)itemAdapter) + 1;
        Debug.Assert(removeIndex != -1, $"expecting item {itemAdapter.Label} to be in the shown list");

        await this.HideChildrenRecursiveAsync(itemAdapter, removeIndex).ConfigureAwait(false);
    }

    private async Task HideChildrenRecursiveAsync(ITreeItem parent, int removeIndex)
    {
        foreach (var child in await parent.Children.ConfigureAwait(false))
        {
            this.ShownItems.RemoveAt(removeIndex);
            if (child.IsExpanded)
            {
                await this.HideChildrenRecursiveAsync(child, removeIndex).ConfigureAwait(false);
            }
        }
    }
}
