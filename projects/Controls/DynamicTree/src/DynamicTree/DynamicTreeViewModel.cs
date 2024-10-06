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

    public async Task ExpandItemAsync(TreeItemAdapter itemAdapter)
    {
        if (itemAdapter.IsExpanded)
        {
            return;
        }

        await this.RestoreExpandedChildrenAsync(itemAdapter).ConfigureAwait(false);
        itemAdapter.IsExpanded = true;
    }

    public async Task CollapseItemAsync(TreeItemAdapter itemAdapter)
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

    private async Task RemoveItem(ITreeItem item)
    {
        // Remove the item's children then the item recursively
        var children = await item.Children.ConfigureAwait(false);
        for (var childIndex = children.Count - 1; childIndex >= 0; childIndex--)
        {
            await this.RemoveItem(children[childIndex]).ConfigureAwait(false);
        }

        if (item.Parent != null)
        {
            await item.Parent.RemoveChildAsync(item).ConfigureAwait(false);
        }

        // TODO: should add events for item add, delete, move to different parent
        this.ShownItems.Remove(item);
    }

    [RelayCommand(CanExecute = nameof(HasSelectedItems))]
    private async Task RemoveSelectedItems()
    {
        if (this.selectionModel?.IsEmpty ?? false)
        {
            return;
        }

        if (this.selectionModel is SingleSelectionModel singleSelection)
        {
            var selectedItem = singleSelection.SelectedItem;
            if (selectedItem is not null /* TODO: add possibility to protect item against delete */)
            {
                this.selectionModel.ClearSelection();
                await this.RemoveItem(selectedItem).ConfigureAwait(false);
            }
        }

        if (this.selectionModel is MultipleSelectionModel multipleSelection)
        {
            var selectedIndices = multipleSelection.SelectedIndices.ToList();

            // Clear the selection before we start modifying the shown items
            // collection so that the indices are still valid while updating the
            // items being deselected.
            this.selectionModel.ClearSelection();

            // Sort selected indices in descending order to avoid index shifting issues
            selectedIndices.Sort((a, b) => b.CompareTo(a));

            var tasks = selectedIndices
                .Select(index => this.ShownItems[index])
                /* .Where(this.CanRemoveItem) TODO: add possibility to protect item against delete */
                .Select(async item => await this.RemoveItem(item).ConfigureAwait(false));

            await Task.WhenAll(tasks).ConfigureAwait(false);
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
