// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.TreeView;

using System.Collections.ObjectModel;
using System.Diagnostics;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

/// <summary>
/// Base class for a view model supporting a tree-based view.
/// </summary>
/// <param name="showRoot">
/// Indicates if the tree root node show be shown or not.
/// </param>
public partial class TreeViewModelBase(bool showRoot = true) : ObservableObject
{
    [ObservableProperty]
    private ITreeItem? selectedItem;

    private ITreeItem? root;

    public ObservableCollection<ITreeItem> ShownItems { get; } = [];

    protected ITreeItem? Root
    {
        get => this.root;
        set
        {
            this.root = value;
            this.ShownItems.Clear();
            if (this.root is null)
            {
                return;
            }

            if (showRoot)
            {
                this.ShownItems.Add(this.root);
                this.ToggleExpanded(this.root);
            }
            else
            {
                foreach (var child in this.root.Children)
                {
                    this.ShownItems.Add(child);
                }
            }
        }
    }

    [RelayCommand]
    private void ToggleExpanded(ITreeItem itemAdapter)
    {
        if (!itemAdapter.HasChildren)
        {
            return;
        }

        if (itemAdapter.IsExpanded)
        {
            this.CollapseItem(itemAdapter);
        }
        else
        {
            this.ExpandItem(itemAdapter);
        }
    }

    private void ExpandItem(ITreeItem itemAdapter)
    {
        if (itemAdapter.IsExpanded)
        {
            return;
        }

        itemAdapter.IsExpanded = true;
        this.RestoreExpandedChildren(itemAdapter);
    }

    private void CollapseItem(ITreeItem itemAdapter)
    {
        if (!itemAdapter.IsExpanded)
        {
            return;
        }

        itemAdapter.IsExpanded = false;
        this.HideChildren(itemAdapter);
    }

    private void RestoreExpandedChildren(ITreeItem itemAdapter)
    {
        var insertIndex = this.ShownItems.IndexOf(itemAdapter);
        Debug.Assert(insertIndex != -1, $"expecting item {itemAdapter.Label} to be in the shown list");
        this.RestoreExpandedChildrenRecursive(itemAdapter, insertIndex);
    }

    private void RestoreExpandedChildrenRecursive(ITreeItem parent, int insertIndex)
    {
        foreach (var child in parent.Children)
        {
            this.ShownItems.Insert(++insertIndex, child);
            if (child.IsExpanded)
            {
                this.RestoreExpandedChildrenRecursive(child, insertIndex);
            }
        }
    }

    private void HideChildren(ITreeItem itemAdapter)
    {
        var removeIndex = this.ShownItems.IndexOf(itemAdapter) + 1;
        Debug.Assert(removeIndex != -1, $"expecting item {itemAdapter.Label} to be in the shown list");

        this.HideChildrenRecursive(itemAdapter, removeIndex);
    }

    private void HideChildrenRecursive(ITreeItem parent, int removeIndex)
    {
        foreach (var child in parent.Children)
        {
            this.ShownItems.RemoveAt(removeIndex);
            if (child.IsExpanded)
            {
                this.HideChildrenRecursive(child, removeIndex);
            }
        }
    }
}
