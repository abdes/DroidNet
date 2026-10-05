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
    private ITreeItem? displayedSelectionAnchor;
    private int selectionBatchDepth;

    /// <summary>
    ///     Raised once after a selection entry point has fully settled, so consumers publish the
    ///     completed membership and active identity together instead of re-publishing partial
    ///     intermediate states for every item notification.
    /// </summary>
    public event EventHandler? SelectionSettled;

    /// <summary>
    ///     Gets or sets the selection mode for the tree view.
    /// </summary>
    [ObservableProperty]
    public partial SelectionMode SelectionMode { get; set; } = SelectionMode.None;

    /// <summary>
    ///     Gets the item the user last explicitly selected through a pointer, keyboard or
    ///     programmatic selection request — the active identity of the selection, independent of
    ///     selection order. <see langword="null" /> when no selected item was explicitly activated.
    /// </summary>
    public ITreeItem? ActiveItem { get; private set; }

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
    ///     Gets a value indicating whether a selection entry point is mid-flight. Consumers that
    ///     observe the selection model directly should defer their own notifications while this is
    ///     set and handle <see cref="SelectionSettled" /> instead.
    /// </summary>
    protected bool IsSelectionBatchActive => this.selectionBatchDepth > 0;

    /// <summary>
    ///     Marks the item that carries the explicit active selection identity, typically while applying
    ///     a caller-resolved selection where no interactive selection path ran.
    /// </summary>
    /// <param name="item">The item to treat as the active identity, or <see langword="null" /> to clear.</param>
    protected void SetActiveItem(ITreeItem? item) => this.ActiveItem = item;

    /// <summary>
    ///     Consumer selection policy: whether <paramref name="candidate" /> may join
    ///     <paramref name="currentlySelected" /> as an additional selected row. The default allows every
    ///     combination; consumers override to make specific rows exclusive (for example a root row that
    ///     can never share a selection). When a candidate is refused, the selection becomes exclusive to
    ///     that candidate.
    /// </summary>
    /// <param name="candidate">The row the user is trying to add to the selection.</param>
    /// <param name="currentlySelected">The rows currently selected.</param>
    /// <returns><see langword="true" /> when the candidate may coexist with the current selection.</returns>
    protected virtual bool AllowsCoSelection(ITreeItem candidate, IReadOnlyList<ITreeItem> currentlySelected) => true;

    /// <summary>
    ///     Consumer selection policy: whether <paramref name="item" /> participates in range and bulk
    ///     (select-all, toggle, invert) selections. The default includes every row.
    /// </summary>
    /// <param name="item">The row under consideration.</param>
    /// <returns><see langword="true" /> when the row may be selected by range or bulk operations.</returns>
    protected virtual bool IsIncludedInBulk(ITreeItem item) => true;

    /// <summary>
    ///     Runs <paramref name="apply" /> as one selection transaction: item notifications raised
    ///     inside it are visible through <see cref="IsSelectionBatchActive" />, and
    ///     <see cref="SelectionSettled" /> is raised once when the outermost transaction completes.
    /// </summary>
    /// <param name="apply">The selection mutation to batch.</param>
    protected void WithSelectionBatch(Action apply)
    {
        ArgumentNullException.ThrowIfNull(apply);
        if (this.selectionBatchDepth > 0)
        {
            // Already inside a transaction; the outer owner raises the settle notification.
            apply();
            return;
        }

        this.selectionBatchDepth++;
        try
        {
            apply();
        }
        finally
        {
            this.selectionBatchDepth--;
            this.SelectionSettled?.Invoke(this, EventArgs.Empty);
        }
    }

    /// <summary>Selects an item using a caller-supplied visible range while storing canonical shown-item selection.</summary>
    /// <param name="item">The target item.</param>
    /// <param name="displayedItems">The current rendered item order.</param>
    /// <param name="isControlDown">Whether selection should be additive/toggle.</param>
    /// <param name="isShiftDown">Whether to select the displayed range from the prior anchor.</param>
    public void SelectDisplayedItem(ITreeItem item, IReadOnlyList<ITreeItem> displayedItems, bool isControlDown, bool isShiftDown)
    {
        ArgumentNullException.ThrowIfNull(item);
        ArgumentNullException.ThrowIfNull(displayedItems);
        if (this.SelectionMode == SelectionMode.None || !this.shownItems.Contains(item) || !displayedItems.Contains(item))
        {
            return;
        }

        this.WithSelectionBatch(() => this.ApplyDisplayedSelection(item, displayedItems, isControlDown, isShiftDown));
    }

    /// <summary>Toggles selection of the items currently displayed by the control.</summary>
    /// <param name="displayedItems">The current rendered item order.</param>
    public void ToggleDisplayedSelection(IReadOnlyList<ITreeItem> displayedItems)
    {
        ArgumentNullException.ThrowIfNull(displayedItems);
        if (this.SelectionMode != SelectionMode.Multiple || this.SelectionModel is not { } selection)
        {
            return;
        }

        this.WithSelectionBatch(() =>
        {
            var bulkItems = displayedItems.Where(this.IsIncludedInBulk).ToArray();
            if (bulkItems.Length > 0 && bulkItems.All(static item => item.IsSelected))
            {
                foreach (var item in bulkItems)
                {
                    selection.ClearSelection(item);
                }

                this.ClearExcludedFromSelection();
                this.ForgetActiveItemIfDeselected();
                return;
            }

            this.ClearExcludedFromSelection();
            foreach (var item in bulkItems.Where(this.shownItems.Contains))
            {
                selection.SelectItem(item);
            }
        });
    }

    /// <summary>Inverts selection only among the items currently displayed by the control.</summary>
    /// <param name="displayedItems">The current rendered item order.</param>
    public void InvertDisplayedSelection(IReadOnlyList<ITreeItem> displayedItems)
    {
        ArgumentNullException.ThrowIfNull(displayedItems);
        if (this.SelectionMode != SelectionMode.Multiple || this.SelectionModel is not { } selection)
        {
            return;
        }

        this.WithSelectionBatch(() =>
        {
            this.ClearExcludedFromSelection();
            foreach (var item in displayedItems.Where(this.IsIncludedInBulk))
            {
                if (!this.shownItems.Contains(item))
                {
                    continue;
                }

                if (item.IsSelected)
                {
                    selection.ClearSelection(item);
                }
                else
                {
                    selection.SelectItem(item);
                }
            }

            this.ForgetActiveItemIfDeselected();
        });
    }

    /// <summary>Synchronizes the replacement selection model with the shown items.</summary>
    /// <param name="oldValue">The previous selection model.</param>
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

        this.WithSelectionBatch(() => this.ApplyCommandSelection(sm!, args));
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

        this.WithSelectionBatch(() =>
        {
            var included = this.shownItems.Where(this.IsIncludedInBulk).ToArray();
            this.ClearExcludedFromSelection();
            if (multipleSelection.SelectedIndices.Count == included.Length)
            {
                multipleSelection.ClearSelection();
                this.SetActiveItem(item: null);
            }
            else
            {
                this.SelectIncludedRows(multipleSelection, this.shownItems);
            }
        });
    }

    /// <summary>
    ///     Clears the selection of the specified item in the tree view.
    /// </summary>
    /// <param name="item">The item to clear selection for.</param>
    [RelayCommand]
    private void ClearSelection(ITreeItem item)
    {
        this.WithSelectionBatch(() =>
        {
            if (ReferenceEquals(this.ActiveItem, item))
            {
                this.SetActiveItem(item: null);
            }

            this.SelectionModel?.ClearSelection(item);
        });
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

        this.WithSelectionBatch(() =>
        {
            this.SetActiveItem(item: null);
            this.SelectionModel?.ClearSelection();
        });
    }

    /// <summary>
    ///     Selects all items in the tree view.
    /// </summary>
    [RelayCommand]
    private void SelectAll()
    {
        if (this.SelectionModel is MultipleSelectionModel<ITreeItem> multipleSelection)
        {
            this.WithSelectionBatch(() =>
            {
                this.ClearExcludedFromSelection();
                this.SelectIncludedRows(multipleSelection, this.shownItems);
            });
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

        this.WithSelectionBatch(() =>
        {
            if (this.shownItems.All(this.IsIncludedInBulk))
            {
                multipleSelection.InvertSelection();
                this.ForgetActiveItemIfDeselected();
                return;
            }

            this.ClearExcludedFromSelection();
            foreach (var item in this.shownItems.Where(this.IsIncludedInBulk))
            {
                if (item.IsSelected)
                {
                    multipleSelection.ClearSelection(item);
                }
                else
                {
                    multipleSelection.SelectItem(item);
                }
            }

            this.ForgetActiveItemIfDeselected();
        });
    }

    /// <summary>
    ///     Selects every included row through the selection model's own batched operation, so its
    ///     single-notification reset and deterministic last-row <see cref="SelectedItem" />
    ///     semantics are preserved instead of being replaced by one notification per row. Rows
    ///     excluded by the consumer policy are simply not part of the batch.
    /// </summary>
    private void SelectIncludedRows(MultipleSelectionModel<ITreeItem> multipleSelection, IReadOnlyList<ITreeItem> source)
    {
        var indices = new List<int>(source.Count);
        for (var index = 0; index < source.Count; index++)
        {
            if (this.IsIncludedInBulk(source[index]))
            {
                indices.Add(index);
            }
        }

        if (indices.Count == 0)
        {
            return;
        }

        multipleSelection.SelectItemsAt([.. indices]);
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

        this.SetActiveItem(null);
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
    ///     The display-path selection body, run inside one transaction: the active identity is written
    ///     before the model mutates, so the notification settled at the end carries the final
    ///     membership and primary together.
    /// </summary>
    private void ApplyDisplayedSelection(ITreeItem item, IReadOnlyList<ITreeItem> displayedItems, bool isControlDown, bool isShiftDown)
    {
        if (this.SelectionMode == SelectionMode.Single)
        {
            this.SetActiveItem(item);
            this.SelectionModel?.ClearAndSelectItem(item);
            this.displayedSelectionAnchor = item;
            return;
        }

        var selection = this.SelectionModel;
        if (selection is null)
        {
            return;
        }

        if (isShiftDown)
        {
            var anchor = this.displayedSelectionAnchor is { } currentAnchor && displayedItems.Contains(currentAnchor)
                ? currentAnchor
                : selection.SelectedItem is { } selected && displayedItems.Contains(selected) ? selected : item;
            var first = FindDisplayedIndex(displayedItems, anchor);
            var last = FindDisplayedIndex(displayedItems, item);
            if (!this.IsCoSelectionAllowed(item) || !this.IsIncludedInBulk(item))
            {
                // The clicked end is policy-excluded from ranges (for example an exclusive row):
                // it becomes the whole selection instead of widening the range onto it.
                this.SetActiveItem(item);
                selection.ClearAndSelectItem(item);
                this.displayedSelectionAnchor = item;
                return;
            }

            this.SetActiveItem(item);
            if (!isControlDown)
            {
                selection.ClearSelection();
            }

            for (var index = Math.Min(first, last); index <= Math.Max(first, last); index++)
            {
                var ranged = displayedItems[index];
                if (this.IsIncludedInBulk(ranged))
                {
                    selection.SelectItem(ranged);
                }
            }

            this.displayedSelectionAnchor = anchor;
            return;
        }

        if (isControlDown)
        {
            if (item.IsSelected)
            {
                if (ReferenceEquals(this.ActiveItem, item))
                {
                    this.SetActiveItem(item: null);
                }

                selection.ClearSelection(item);
            }
            else
            {
                this.SetActiveItem(item);
                if (!this.IsCoSelectionAllowed(item))
                {
                    selection.ClearAndSelectItem(item);
                }
                else
                {
                    selection.SelectItem(item);
                }
            }
        }
        else
        {
            this.SetActiveItem(item);
            selection.ClearAndSelectItem(item);
        }

        this.displayedSelectionAnchor = item;

        static int FindDisplayedIndex(IReadOnlyList<ITreeItem> items, ITreeItem target)
        {
            for (var index = 0; index < items.Count; index++)
            {
                if (ReferenceEquals(items[index], target))
                {
                    return index;
                }
            }

            return -1;
        }
    }

    /// <summary>
    ///     The command-path selection body, run inside one transaction with the active identity
    ///     written before mutation.
    /// </summary>
    private void ApplyCommandSelection(SelectionModel<ITreeItem> sm, ItemSelectionArgs args)
    {
        if (this.SelectionMode == SelectionMode.Single)
        {
            this.SetActiveItem(args.Item);
            sm.ClearAndSelectItem(args.Item);
            return;
        }

        // When in multiple-selection mode and there is no existing selection, prefer
        // adding the item (emit Add) rather than using ClearAndSelectItem which may
        // batch into a Reset notification.
        if (this.SelectionMode == SelectionMode.Multiple && !args.IsShiftKeyDown && !args.IsCtrlKeyDown && sm.IsEmpty)
        {
            this.SetActiveItem(args.Item);
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
                if (ReferenceEquals(this.ActiveItem, args.Item))
                {
                    this.SetActiveItem(item: null);
                }

                sm.ClearSelection(args.Item);
            }
            else
            {
                this.SetActiveItem(args.Item);
                if (!this.IsCoSelectionAllowed(args.Item))
                {
                    sm.ClearAndSelectItem(args.Item);
                }
                else
                {
                    sm.SelectItem(args.Item);
                }
            }

            return;
        }

        this.SetActiveItem(args.Item);
        sm.ClearAndSelectItem(args.Item);
    }

    /// <summary>
    ///     Extends the selection to the specified item in the tree view.
    /// </summary>
    /// <param name="item">The item to extend selection to.</param>
    private void ExtendSelectionTo(ITreeItem item)
    {
        if (this.SelectionMode == SelectionMode.Multiple && this.SelectionModel?.SelectedItem is not null)
        {
            var multi = (MultipleSelectionModel<ITreeItem>)this.SelectionModel;
            this.SetActiveItem(item);
            if (!this.IsIncludedInBulk(item))
            {
                // A policy-excluded target (for example an exclusive row) becomes the whole
                // selection rather than dragging the range onto it.
                multi.ClearAndSelectItem(item);
                return;
            }

            multi.SelectRange(
                this.SelectionModel.SelectedItem,
                item);

            foreach (var index in multi.SelectedIndices.ToArray())
            {
                var selected = this.GetShownItemAt(index);
                if (!this.IsIncludedInBulk(selected))
                {
                    multi.ClearSelection(selected);
                }
            }
        }
        else
        {
            // We diverge from the default behavior of SelectItem here to throw an exception if the
            // item is not shown in the tree
            if (!this.shownItems.Contains(item))
            {
                throw new ArgumentException("item not found", nameof(item));
            }

            this.SetActiveItem(item);
            this.SelectionModel?.SelectItem(item);
        }
    }

    /// <summary>
    ///     Applies the consumer co-selection policy to one candidate against the current selection.
    ///     An empty current selection always admits the candidate.
    /// </summary>
    private bool IsCoSelectionAllowed(ITreeItem candidate)
    {
        if (this.SelectionModel is null)
        {
            return true;
        }

        if (this.SelectionModel is MultipleSelectionModel<ITreeItem> multi)
        {
            return multi.SelectedIndices.Count == 0
                || this.AllowsCoSelection(candidate, [.. multi.SelectedIndices.Select(this.GetShownItemAt)]);
        }

        return this.SelectionModel.SelectedItem is not { } selected || this.AllowsCoSelection(candidate, [selected]);
    }

    /// <summary>
    ///     Normalizes an existing selection before a bulk operation adds to it: rows the consumer
    ///     policy excludes from bulk participation cannot coexist with the bulk set, so they are
    ///     deselected first instead of lingering into a mixed selection.
    /// </summary>
    private void ClearExcludedFromSelection()
    {
        if (this.SelectionModel is MultipleSelectionModel<ITreeItem> multi)
        {
            foreach (var index in multi.SelectedIndices.ToArray())
            {
                var item = this.GetShownItemAt(index);
                if (!this.IsIncludedInBulk(item))
                {
                    multi.ClearSelection(item);
                }
            }
        }

        this.ForgetActiveItemIfDeselected();
    }

    /// <summary>
    ///     Drops the active identity when its item no longer carries selection.
    /// </summary>
    private void ForgetActiveItemIfDeselected()
    {
        if (this.ActiveItem is { IsSelected: false })
        {
            this.SetActiveItem(item: null);
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
        protected override int IndexOf(ITreeItem item) => this.model.ShownIndexOf(item);
    }

    /// <summary>
    ///     Represents a selection model that allows multiple items to be selected at a time within the dynamic tree view model.
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
        protected override int IndexOf(ITreeItem item) => model.ShownIndexOf(item);
    }
}
