// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Controls.Selection;

namespace DroidNet.Controls;

/// <summary>
///     Represents the ViewModel for a dynamic tree control, providing functionality for managing
///     hierarchical data structures, including selection, expansion, and manipulation of tree items.
/// </summary>
public abstract partial class DynamicTreeViewModel
{
    /// <summary>
    ///     Gets or sets the selection mode for the tree view.
    /// </summary>
    [ObservableProperty]
    public partial SelectionMode SelectionMode { get; set; } = SelectionMode.None;

    /// <summary>
    ///     Gets the current selection model for the tree view.
    /// </summary>
    protected SelectionModel<ITreeItem>? SelectionModel { get; private set; }

    /// <summary>
    ///     Selects the specified item in the tree view.
    /// </summary>
    /// <param name="item">The item to select.</param>
    public void SelectItem(ITreeItem item) => this.SelectionModel?.SelectItem(item);

    /// <summary>
    ///     Clears the selection of the specified item in the tree view.
    /// </summary>
    /// <param name="item">The item to clear selection for.</param>
    public void ClearSelection(ITreeItem item) => this.SelectionModel?.ClearSelection(item);

    /// <summary>
    ///     Clears the current selection and selects the specified item in the tree view.
    /// </summary>
    /// <param name="item">The item to select.</param>
    public void ClearAndSelectItem(ITreeItem item) => this.SelectionModel?.ClearAndSelectItem(item);

    /// <summary>
    ///     Extends the selection to the specified item in the tree view.
    /// </summary>
    /// <param name="item">The item to extend selection to.</param>
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
            // We diverge from the default behavior of SelectItem here to throw an exception if the
            // item is not shown in the tree
            if (!this.ShownItems.Contains(item))
            {
                throw new ArgumentException("item not found", nameof(item));
            }

            this.SelectionModel?.SelectItem(item);
        }
    }

    /// <summary>
    ///     Toggles the selection of all items in the tree view.
    /// </summary>
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

    /// <summary>
    ///     Called when the selection model changes.
    /// </summary>
    /// <param name="oldValue">The old selection model.</param>
    protected virtual void OnSelectionModelChanged(SelectionModel<ITreeItem>? oldValue)
        => this.SyncSelectionModelWithItems();

    /// <summary>
    ///     Clears the current selection in the tree view.
    /// </summary>
    [RelayCommand]
    private void SelectNone() => this.SelectionModel?.ClearSelection();

    /// <summary>
    ///     Selects all items in the tree view.
    /// </summary>
    [RelayCommand]
    private void SelectAll()
    {
        if (this.SelectionModel is MultipleSelectionModel<ITreeItem> multipleSelection)
        {
            multipleSelection.SelectAll();
        }
    }

    /// <summary>
    ///     Inverts the current selection in the tree view.
    /// </summary>
    [RelayCommand]
    private void InvertSelection()
    {
        if (this.SelectionModel is not MultipleSelectionModel<ITreeItem> multipleSelection)
        {
            return;
        }

        multipleSelection.InvertSelection();
    }

    /// <summary>
    ///     Called when the selection mode changes.
    /// </summary>
    /// <param name="value">The new selection mode.</param>
    partial void OnSelectionModeChanged(SelectionMode value)
    {
        var oldValue = this.SelectionModel;

        this.SelectionModel = value switch
        {
            SelectionMode.None => null,
            SelectionMode.Single => new SingleSelectionModel(this),
            SelectionMode.Multiple => new MultipleSelectionModel(this),
            _ => throw new InvalidEnumArgumentException(nameof(value), (int)value, typeof(SelectionMode)),
        };

        this.OnSelectionModelChanged(oldValue);
    }

    /// <summary>
    ///     If we have a selection model, ensure that all shown items that are marked as selected,
    ///     are selected in the selection model. Should be called after the shown items collection
    ///     is initialized for the first time, or when the selection model has changed.
    /// </summary>
    private void SyncSelectionModelWithItems()
    {
        // selection state of items
        if (this.SelectionModel is null)
        {
            return;
        }

        for (var index = 0; index < this.ShownItems.Count; index++)
        {
            if (this.ShownItems[index].IsSelected)
            {
                this.SelectionModel.SelectItemAt(index);
            }
        }
    }

    /// <summary>
    ///     Represents a selection model that allows only a single item to be selected at a time within
    ///     the dynamic tree view model.
    /// </summary>
    /// <remarks>
    ///     This class extends the <see cref="SingleSelectionModel{T}" /> to track single selection in the
    ///     <see cref="ShownItems" /> of a dynamic tree and update the selection state of the items accordingly.
    /// </remarks>
    protected partial class SingleSelectionModel : SingleSelectionModel<ITreeItem>
    {
        private readonly DynamicTreeViewModel model;

        /// <summary>
        ///     Initializes a new instance of the <see cref="SingleSelectionModel" /> class.
        /// </summary>
        /// <param name="model">The dynamic tree view model that this selection model is associated with.</param>
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

        /// <inheritdoc />
        protected override ITreeItem GetItemAt(int index) => this.model.ShownItems[index];

        /// <inheritdoc />
        protected override int GetItemCount() => this.model.ShownItems.Count;

        /// <inheritdoc />
        protected override int IndexOf(ITreeItem item) => this.model.ShownItems.IndexOf((TreeItemAdapter)item);
    }

    /// <summary>
    ///     Represents a selection model that allows multiple items to be selected at a time within the dynamic tree view
    ///     model.
    /// </summary>
    /// <param name="model">The dynamic tree view model that this selection model is associated with.</param>
    /// <remarks>
    ///     This class extends the <see cref="MultipleSelectionModel{T}" /> to track multiple selected items in the
    ///     <see cref="ShownItems" /> of a dynamic tree and update the selection state of the items accordingly.
    /// </remarks>
    protected partial class MultipleSelectionModel(DynamicTreeViewModel model) : MultipleSelectionModel<ITreeItem>
    {
        /// <inheritdoc />
        protected override ITreeItem GetItemAt(int index) => model.ShownItems[index];

        /// <inheritdoc />
        protected override int GetItemCount() => model.ShownItems.Count;

        /// <inheritdoc />
        protected override int IndexOf(ITreeItem item) => model.ShownItems.IndexOf((TreeItemAdapter)item);
    }
}
