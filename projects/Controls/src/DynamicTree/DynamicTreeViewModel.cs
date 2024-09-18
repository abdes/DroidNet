// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.DynamicTree;

using System.Collections.ObjectModel;
using System.Diagnostics;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

public abstract partial class DynamicTreeViewModel(bool showRoot = true) : ObservableObject
{
    [ObservableProperty]
    private ITreeItem? activeItem;

    public ObservableCollection<ITreeItem> ShownItems { get; } = [];

    public async Task InitializeRootAsync(ITreeItem root)
    {
        this.ShownItems.Clear();
        if (root is null)
        {
            return;
        }

        if (showRoot)
        {
            // Add the root item and check if it needs to be expanded
            this.ShownItems.Add(root);
            if (root.IsExpanded)
            {
                await this.RestoreExpandedChildrenAsync(root).ConfigureAwait(false);
            }
        }
        else
        {
            // Do not add the root item, add its children instead and check if it they need to be expanded
            foreach (var child in await root.Children.ConfigureAwait(false))
            {
                this.ShownItems.Add(child);
                if (child.IsExpanded)
                {
                    await this.RestoreExpandedChildrenAsync(child).ConfigureAwait(false);
                }
            }
        }
    }

    partial void OnActiveItemChanged(ITreeItem? oldValue, ITreeItem? newValue)
    {
        if (oldValue is not null)
        {
            oldValue.IsSelected = false;
        }

        if (newValue is not null)
        {
            newValue.IsSelected = true;
        }
    }

    [RelayCommand]
    private async Task ToggleExpanded(ITreeItem itemAdapter)
    {
        if (itemAdapter.IsExpanded)
        {
            await this.CollapseItem(itemAdapter).ConfigureAwait(true);
        }
        else
        {
            await this.ExpandItemAsync(itemAdapter).ConfigureAwait(true);
        }
    }

    [RelayCommand]
    private async Task ExpandItemAsync(ITreeItem itemAdapter)
        => await this.RestoreExpandedChildrenAsync(itemAdapter).ConfigureAwait(false);

    [RelayCommand]
    private async Task CollapseItem(ITreeItem itemAdapter)
        => await this.HideChildrenAsync(itemAdapter).ConfigureAwait(false);

    private async Task RestoreExpandedChildrenAsync(ITreeItem itemAdapter)
    {
        var insertIndex = this.ShownItems.IndexOf(itemAdapter);
        Debug.Assert(insertIndex != -1, $"expecting item {itemAdapter.Label} to be in the shown list");
        await this.RestoreExpandedChildrenRecursive(itemAdapter, insertIndex).ConfigureAwait(false);
    }

    private async Task RestoreExpandedChildrenRecursive(ITreeItem parent, int insertIndex)
    {
        foreach (var child in await parent.Children.ConfigureAwait(false))
        {
            this.ShownItems.Insert(++insertIndex, child);
            if (child.IsExpanded)
            {
                await this.RestoreExpandedChildrenRecursive(child, insertIndex).ConfigureAwait(false);
            }
        }
    }

    private async Task HideChildrenAsync(ITreeItem itemAdapter)
    {
        var removeIndex = this.ShownItems.IndexOf(itemAdapter) + 1;
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
