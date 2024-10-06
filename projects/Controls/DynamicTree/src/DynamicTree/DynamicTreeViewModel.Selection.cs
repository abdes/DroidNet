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
    private SelectionModel<ITreeItem>? selectionModel;

    [ObservableProperty]
    private SelectionMode selectionMode = SelectionMode.None;

    public bool HasSelectedItems => !this.selectionModel?.IsEmpty ?? false;

    public void SelectItem(ITreeItem item) => this.selectionModel?.SelectItem(item);

    public void ClearAndSelectItem(ITreeItem item) => this.selectionModel?.ClearAndSelectItem(item);

    public void ExtendSelectionTo(ITreeItem item)
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

    public void ToggleSelectAll()
    {
        if (this.selectionModel is not MultipleSelectionModel<ITreeItem> multipleSelection)
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

    public void ClearSelection(ITreeItem item) => this.selectionModel?.ClearSelection(item);

    [RelayCommand(CanExecute = nameof(HasSelectedItems))]
    internal void SelectNone() => this.selectionModel?.ClearSelection();

    [RelayCommand]
    private void SelectAll()
    {
        if (this.selectionModel is MultipleSelectionModel<ITreeItem> multipleSelection)
        {
            multipleSelection.SelectAll();
        }
    }

    partial void OnSelectionModeChanged(SelectionMode value)
    {
        if (this.selectionModel is not null)
        {
            this.selectionModel.PropertyChanged -= this.SelectionModel_OnPropertyChanged;
        }

        this.selectionModel = value switch
        {
            SelectionMode.None => default,
            SelectionMode.Single => new SingleSelectionModel(this),
            SelectionMode.Multiple => new MultipleSelectionModel(this),
            _ => throw new InvalidEnumArgumentException(nameof(value), (int)value, typeof(SelectionMode)),
        };

        if (this.selectionModel is not null)
        {
            this.selectionModel.PropertyChanged += this.SelectionModel_OnPropertyChanged;
        }
    }

    private void SelectionModel_OnPropertyChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (string.Equals(args.PropertyName, nameof(SelectionModel<ITreeItem>.IsEmpty), StringComparison.Ordinal))
        {
            // Notify that HasSelectedItems has changed
            this.OnPropertyChanged(nameof(this.HasSelectedItems));
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
