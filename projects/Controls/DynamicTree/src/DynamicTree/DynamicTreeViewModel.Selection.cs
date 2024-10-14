// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

using System.ComponentModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

/// <summary>
/// Selection functionality for <see cref="DynamicTreeViewModel" />.
/// </summary>
public abstract partial class DynamicTreeViewModel
{
    [ObservableProperty]
    private SelectionMode selectionMode = SelectionMode.None;

    protected SelectionModel<ITreeItem>? SelectionModel { get; private set; }

    public void SelectItem(ITreeItem item) => this.SelectionModel?.SelectItem(item);

    public void ClearSelection(ITreeItem item) => this.SelectionModel?.ClearSelection(item);

    public void ClearAndSelectItem(ITreeItem item) => this.SelectionModel?.ClearAndSelectItem(item);

    public void ExtendSelectionTo(ITreeItem item)
    {
        if (this.SelectionMode == SelectionMode.Multiple && this.SelectionModel?.SelectedItem is not null)
        {
            ((MultipleSelectionModel<ITreeItem>)this.SelectionModel).SelectRange(
                this.SelectionModel.SelectedItem,
                item);
        }
        else
        {
            this.SelectionModel?.SelectItem(item);
        }
    }

    public void ToggleSelectAll()
    {
        if (this.SelectionModel is not MultipleSelectionModel<ITreeItem> multipleSelection)
        {
            return;
        }

        if (multipleSelection.SelectedIndices.Count == this.ShownItems.Count)
        {
            multipleSelection.ClearSelection();
        }
        else
        {
            multipleSelection.SelectAll();
        }
    }

    protected virtual void OnSelectionModelChanged(SelectionModel<ITreeItem>? oldValue)
    {
    }

    [RelayCommand]
    private void SelectNone() => this.SelectionModel?.ClearSelection();

    [RelayCommand]
    private void SelectAll()
    {
        if (this.SelectionModel is MultipleSelectionModel<ITreeItem> multipleSelection)
        {
            multipleSelection.SelectAll();
        }
    }

    [RelayCommand]
    private void InvertSelection()
    {
        if (this.SelectionModel is not MultipleSelectionModel<ITreeItem> multipleSelection)
        {
            return;
        }

        multipleSelection.InvertSelection();
    }

    partial void OnSelectionModeChanged(SelectionMode value)
    {
        var oldValue = this.SelectionModel;

        this.SelectionModel = value switch
        {
            SelectionMode.None => default,
            SelectionMode.Single => new SingleSelectionModel(this),
            SelectionMode.Multiple => new MultipleSelectionModel(this),
            _ => throw new InvalidEnumArgumentException(nameof(value), (int)value, typeof(SelectionMode)),
        };

        this.OnSelectionModelChanged(oldValue);
    }

    /// <summary>
    /// An implementation of the <see cref="SingleSelectionModel{T}" /> for the <see cref="DynamicTreeViewModel" />.
    /// </summary>
    protected partial class SingleSelectionModel : SingleSelectionModel<ITreeItem>
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

    /// <summary>
    /// An implementation of the <see cref="MultipleSelectionModel{T}" /> for the <see cref="DynamicTreeViewModel" />.
    /// </summary>
    /// <param name="model">The associated <see cref="DynamicTreeViewModel" />.</param>
    protected partial class MultipleSelectionModel(DynamicTreeViewModel model) : MultipleSelectionModel<ITreeItem>
    {
        protected override ITreeItem GetItemAt(int index) => model.ShownItems[index];

        protected override int GetItemCount() => model.ShownItems.Count;

        protected override int IndexOf(ITreeItem item) => model.ShownItems.IndexOf((TreeItemAdapter)item);
    }
}
