// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

/// <summary>
/// A ViewModel for the <see cref="DynamicTree" /> control.
/// </summary>
public abstract partial class DynamicTreeViewModel : ObservableObject
{
    private SelectionModel<ITreeItem>? selectionModel;

    [ObservableProperty]
    private SelectionMode selectionMode = SelectionMode.None;

    public ObservableCollection<TreeItemAdapter> ShownItems { get; } = [];

    public void SelectItem(TreeItemAdapter item) => this.selectionModel?.SelectItem(item);

    public void ClearAndSelectItem(TreeItemAdapter item) => this.selectionModel?.ClearAndSelectItem(item);

    public void ExtendSelectionTo(TreeItemAdapter item)
    {
        if (this.SelectionMode == SelectionMode.Multiple && this.selectionModel?.SelectedItem is not null)
        {
            ((MultipleSelectionModel<ITreeItem>)this.selectionModel).SelectRange(
                this.selectionModel.SelectedItem,
                item);
        }
        else
        {
            this.selectionModel?.SelectItem(item);
        }
    }

    protected async Task InitializeRootAsync(ITreeItem root)
    {
        this.ShownItems.Clear();

        // Do not add the root item, add its children instead and check if it they need to be expanded
        await this.RestoreExpandedChildrenAsync((TreeItemAdapter)root).ConfigureAwait(false);
    }

    partial void OnSelectionModeChanged(SelectionMode value) =>
        this.selectionModel = value switch
        {
            SelectionMode.None => default,
            SelectionMode.Single => new SingleSelectionModel(this),
            SelectionMode.Multiple => new MultipleSelectionModel(this),
            _ => throw new InvalidEnumArgumentException(nameof(value), (int)value, typeof(SelectionMode)),
        };

    [RelayCommand]
    private async Task ToggleExpanded(TreeItemAdapter itemAdapter)
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
    private async Task ExpandItemAsync(TreeItemAdapter itemAdapter)
    {
        if (itemAdapter.IsExpanded)
        {
            return;
        }

        await this.RestoreExpandedChildrenAsync(itemAdapter).ConfigureAwait(false);
        itemAdapter.IsExpanded = true;
    }

    [RelayCommand]
    private async Task CollapseItem(TreeItemAdapter itemAdapter)
    {
        if (!itemAdapter.IsExpanded)
        {
            return;
        }

        await this.HideChildrenAsync(itemAdapter).ConfigureAwait(false);
        itemAdapter.IsExpanded = false;
    }

    private async Task RestoreExpandedChildrenAsync(TreeItemAdapter itemAdapter)
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

    protected class SingleSelectionModel : SingleSelectionModel<ITreeItem>
    {
        private readonly DynamicTreeViewModel model;

        public SingleSelectionModel(DynamicTreeViewModel model)
        {
            this.model = model;

            this.PropertyChanging += (sender, args) =>
            {
                _ = sender; // unused

                var propertyName = args.PropertyName;
                if (propertyName?.Equals(nameof(this.SelectedItem), StringComparison.Ordinal) == true
                    && this.SelectedItem is not null)
                {
                    this.SelectedItem.IsSelected = false;
                }
            };

            this.PropertyChanged += (sender, args) =>
            {
                _ = sender; // unused

                var propertyName = args.PropertyName;
                if ((string.IsNullOrEmpty(propertyName)
                     || propertyName.Equals(nameof(this.SelectedItem), StringComparison.Ordinal))
                    && this.SelectedItem is not null)
                {
                    this.SelectedItem.IsSelected = true;
                }
            };
        }

        protected override ITreeItem GetItemAt(int index) => this.model.ShownItems[index];

        protected override int GetItemCount() => this.model.ShownItems.Count;

        protected override int IndexOf(ITreeItem item) => this.model.ShownItems.IndexOf((TreeItemAdapter)item);
    }

    protected class MultipleSelectionModel(DynamicTreeViewModel model) : MultipleSelectionModel<ITreeItem>
    {
        protected override ITreeItem GetItemAt(int index) => model.ShownItems[index];

        protected override int GetItemCount() => model.ShownItems.Count;

        protected override int IndexOf(ITreeItem item) => model.ShownItems.IndexOf((TreeItemAdapter)item);
    }
}
