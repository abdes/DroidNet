// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics;
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
    ///     Gets the currently selected item, if any.
    /// </summary>
    public ITreeItem? SelectedItem => this.SelectionModel?.SelectedItem;

    /// <summary>
    ///     Gets the number of currently selected items.
    /// </summary>
    public int SelectedItemsCount
        => this.SelectionModel is MultipleSelectionModel<ITreeItem> multi
            ? multi.SelectedIndices.Count
            : this.SelectionModel?.SelectedIndex != -1 ? 1 : 0;

    /// <summary>
    ///     Gets the current selection model for the tree view.
    /// </summary>
    protected SelectionModel<ITreeItem>? SelectionModel { get; private set; }

    /// <summary>
    ///     Called when the selection model changes. Sunchronized the selection
    ///     model with the shown items and their selection state.
    /// </summary>
    /// <param name="oldValue">The old selection model.</param>
    protected virtual void OnSelectionModelChanged(SelectionModel<ITreeItem>? oldValue) =>
        this.SyncSelectionModelWithItems();

    [RelayCommand]
    private void SelectItem(ItemSelectionArgs args)
    {
        if (this.SelectionMode == SelectionMode.None)
        {
            return;
        }

        // Ignore selection attempts for items that are not currently shown in the tree
        if (!this.shownItems.Contains(args.Item))
        {
            return;
        }

        var sm = this.SelectionModel;
        Debug.Assert(sm is not null, "SelectionModel should not be null when SelectionMode is not None.");

        if (args.Origin != RequestOrigin.Programmatic)
        {
            if (this.FocusedItem is null || !ReferenceEquals(this.FocusedItem.Item, args.Item))
            {
                this.LogForgotToFocusItem(args.Item, args.Origin!);
            }
        }

        if (this.SelectionMode == SelectionMode.Single)
        {
            sm.ClearAndSelectItem(args.Item);
            return;
        }

        // When in multiple-selection mode and there is no existing selection, prefer
        // adding the item (emit Add) rather than using ClearAndSelectItem which may
        // batch into a Reset notification.
        if (this.SelectionMode == SelectionMode.Multiple && !args.IsShiftKeyDown && !args.IsCtrlKeyDown && sm.IsEmpty)
        {
            sm.SelectItem(args.Item);
            return;
        }

        if (args.IsShiftKeyDown)
        {
            this.ExtendSelectionTo(args.Item);
            return;
        }

        if (args.IsCtrlKeyDown)
        {
            if (args.Item.IsSelected)
            {
                sm.ClearSelection(args.Item);
            }
            else
            {
                sm.SelectItem(args.Item);
            }

            return;
        }

        sm.ClearAndSelectItem(args.Item);
    }

    /// <summary>
    ///     Toggles the selection of all items in the tree view.
    /// </summary>
    [RelayCommand]
    private void ToggleSelectAll()
    {
        if (this.SelectionModel is not MultipleSelectionModel<ITreeItem> multipleSelection)
        {
            return;
        }

        if (multipleSelection.SelectedIndices.Count == this.shownItems.Count)
        {
            multipleSelection.ClearSelection();
        }
        else
        {
            multipleSelection.SelectAll();
        }
    }

    /// <summary>
    ///     Clears the selection of the specified item in the tree view.
    /// </summary>
    /// <param name="item">The item to clear selection for.</param>
    [RelayCommand]
    private void ClearSelection(ITreeItem item) => this.SelectionModel?.ClearSelection(item);

    /// <summary>
    ///     Extends the selection to the specified item in the tree view.
    /// </summary>
    /// <param name="item">The item to extend selection to.</param>
    private void ExtendSelectionTo(ITreeItem item)
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
            if (!this.shownItems.Contains(item))
            {
                throw new ArgumentException("item not found", nameof(item));
            }

            this.SelectionModel?.SelectItem(item);
        }
    }

    /// <summary>
    ///     Clears the current selection in the tree view.
    /// </summary>
    [RelayCommand]
    private void SelectNone()
    {
        if (this.SelectionModel?.IsEmpty == true)
        {
            // Avoid side effects
            return;
        }

        this.SelectionModel?.ClearSelection();
    }

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

        for (var index = 0; index < this.shownItems.Count; index++)
        {
            if (this.shownItems[index].IsSelected)
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
        protected override ITreeItem GetItemAt(int index) => this.model.GetShownItemAt(index);

        /// <inheritdoc />
        protected override int GetItemCount() => this.model.ShownItemsCount;

        /// <inheritdoc />
        protected override int IndexOf(ITreeItem item) => this.model.ShownIndexOf((TreeItemAdapter)item);
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
        protected override ITreeItem GetItemAt(int index) => model.GetShownItemAt(index);

        /// <inheritdoc />
        protected override int GetItemCount() => model.ShownItemsCount;

        /// <inheritdoc />
        protected override int IndexOf(ITreeItem item) => model.ShownIndexOf((TreeItemAdapter)item);
    }
}
