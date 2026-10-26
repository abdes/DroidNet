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
    private ITreeItem? displayedSelectionAnchor;
    private int selectionBatchDepth;

    /// <summary>
    ///     Raised once after a selection entry point has fully settled, so consumers publish the
    ///     completed membership and active identity together instead of re-publishing partial
    ///     intermediate states for every item notification. It is not raised when the entry point failed.
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
    public ITreeItem? ActiveItem
    {
        get;
        private set => this.SetProperty(ref field, value);
    }

    /// <summary>
    ///     Gets the currently selected item, if any.
    /// </summary>
    public ITreeItem? SelectedItem => this.SelectionModel?.SelectedItem;

    /// <summary>
    ///     Gets the number of currently selected items.
    /// </summary>
    public int SelectedItemsCount
        => this.SelectionModel switch
        {
            MultipleSelectionModel<ITreeItem> multi => multi.SelectedIndices.Count,
            { IsEmpty: false } => 1,
            _ => 0,
        };

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
    ///     <see cref="SelectionSettled" /> is raised once when the outermost transaction completes
    ///     successfully.
    /// </summary>
    /// <param name="apply">The selection mutation to batch.</param>
    protected void WithSelectionBatch(Action apply) => this.RunSelectionBatch(apply, raiseSettled: null);

    /// <summary>Selects an item using a caller-supplied visible range while storing canonical shown-item selection.</summary>
    /// <param name="item">The target item.</param>
    /// <param name="displayedItems">The current rendered item order.</param>
    /// <param name="isControlDown">Whether selection should be additive/toggle.</param>
    /// <param name="isShiftDown">Whether to select the displayed range from the prior anchor.</param>
    public void SelectDisplayedItem(ITreeItem item, IReadOnlyList<ITreeItem> displayedItems, bool isControlDown, bool isShiftDown)
    {
        ArgumentNullException.ThrowIfNull(item);
        ArgumentNullException.ThrowIfNull(displayedItems);
        if (this.SelectionMode == SelectionMode.None || !this.shownItems.Contains(item) || IndexOfItem(displayedItems, item) < 0)
        {
            return;
        }

        this.WithSelectionBatch(() => this.ApplySelection(item, displayedItems, isControlDown, isShiftDown));
    }

    /// <summary>Toggles selection of the items currently displayed by the control.</summary>
    /// <param name="displayedItems">The current rendered item order.</param>
    public void ToggleDisplayedSelection(IReadOnlyList<ITreeItem> displayedItems)
    {
        ArgumentNullException.ThrowIfNull(displayedItems);
        if (this.SelectionMode != SelectionMode.Multiple || this.SelectionModel is not MultipleSelectionModel<ITreeItem> selection)
        {
            return;
        }

        this.WithSelectionBatch(() =>
        {
            var bulkItems = displayedItems.Where(this.IsIncludedInBulk).ToArray();
            if (bulkItems.Length > 0 && bulkItems.All(static item => item.IsSelected))
            {
                var bulkIndices = this.ShownIndicesOf(bulkItems).ToHashSet();
                ReplaceSelection(selection, [.. selection.SelectedIndices.Where(index => !bulkIndices.Contains(index))]);
                this.ClearExcludedFromSelection();
                return;
            }

            this.ClearExcludedFromSelection();
            this.SelectAdditionally(selection, bulkItems);
        });
    }

    /// <summary>Inverts selection only among the items currently displayed by the control.</summary>
    /// <param name="displayedItems">The current rendered item order.</param>
    public void InvertDisplayedSelection(IReadOnlyList<ITreeItem> displayedItems)
    {
        ArgumentNullException.ThrowIfNull(displayedItems);
        if (this.SelectionMode != SelectionMode.Multiple || this.SelectionModel is not MultipleSelectionModel<ITreeItem> selection)
        {
            return;
        }

        this.WithSelectionBatch(() => this.InvertAmong(selection, displayedItems));
    }

    /// <summary>Synchronizes the replacement selection model with the shown items.</summary>
    /// <param name="oldValue">The previous selection model.</param>
    protected virtual void OnSelectionModelChanged(SelectionModel<ITreeItem>? oldValue) =>
        this.SyncSelectionModelWithItems();

    private static int IndexOfItem(IReadOnlyList<ITreeItem> items, ITreeItem target)
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

    private static void ReplaceSelection(MultipleSelectionModel<ITreeItem> selection, IReadOnlyList<int> indices)
    {
        if (indices.Count == 0)
        {
            selection.ClearSelection();
            return;
        }

        selection.SelectItemsAt([.. indices]);
    }

    [RelayCommand]
    private void SelectItem(ItemSelectionArgs args)
    {
        if (this.SelectionMode == SelectionMode.None || this.SelectionModel is null)
        {
            return;
        }

        // Ignore selection attempts for items that are not currently shown in the tree
        if (!this.shownItems.Contains(args.Item))
        {
            return;
        }

        if (args.Origin != RequestOrigin.Programmatic)
        {
            if (this.FocusedItem is null || !ReferenceEquals(this.FocusedItem.Item, args.Item))
            {
                this.LogForgotToFocusItem(args.Item, args.Origin);
            }
        }

        this.WithSelectionBatch(() => this.ApplySelection(args.Item, this.shownItems, args.IsCtrlKeyDown, args.IsShiftKeyDown));
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
            var includedCount = this.shownItems.Count(this.IsIncludedInBulk);
            this.ClearExcludedFromSelection();
            if (multipleSelection.SelectedIndices.Count == includedCount)
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
        // Only a shown row is in the selection model; any other item has no selection to clear.
        if (!this.IsShown(item))
        {
            return;
        }

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

            this.InvertAmong(multipleSelection, this.shownItems);
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
        if (value == SelectionMode.None)
        {
            // No model will carry the selection, so the rows must not keep claiming it.
            oldValue?.ClearSelection();
        }

        this.SelectionModel = value switch
        {
            SelectionMode.None => null,
            SelectionMode.Single => new SingleSelectionModel(this),
            SelectionMode.Multiple => new MultipleSelectionModel(this),
            _ => throw new InvalidEnumArgumentException(nameof(value), (int)value, typeof(SelectionMode)),
        };

        this.SetActiveItem(null);
        this.displayedSelectionAnchor = null;
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
    ///     Runs <paramref name="apply" /> as the outermost selection transaction, or inside the one already running.
    /// </summary>
    /// <param name="apply">The selection mutation to batch.</param>
    /// <param name="raiseSettled">
    ///     Decides, after <paramref name="apply" /> succeeded, whether <see cref="SelectionSettled" /> is raised;
    ///     <see langword="null" /> always raises it.
    /// </param>
    private void RunSelectionBatch(Action apply, Func<bool>? raiseSettled)
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
        }

        if (raiseSettled is null || raiseSettled())
        {
            this.SelectionSettled?.Invoke(this, EventArgs.Empty);
        }
    }

    /// <summary>
    ///     Changes the shown rows while keeping the selection on the same items.
    /// </summary>
    /// <param name="reshape">The synchronous insertion or removal of shown rows.</param>
    /// <remarks>
    ///     The selection models store row positions, which a change above a selected row silently
    ///     re-points. The selection is therefore released while the positions still name the right rows,
    ///     and rebuilt from the items afterwards; items that are no longer shown stay deselected. It is
    ///     one selection transaction; <see cref="SelectionSettled" /> reports the final state once, and only when
    ///     the membership or the active item changed, not when the selected rows merely moved.
    /// </remarks>
    private void ReshapeShownItems(Action reshape)
    {
        if (this.SelectionModel is not { IsEmpty: false } selection)
        {
            reshape();
            this.ForgetHiddenAnchor();
            return;
        }

        var selected = new List<ITreeItem>();
        if (selection is MultipleSelectionModel<ITreeItem> multi)
        {
            selected.AddRange(multi.SelectedIndices.Select(this.GetShownItemAt));
        }
        else if (selection.SelectedItem is { } single)
        {
            selected.Add(single);
        }

        var activeBefore = this.ActiveItem;
        var restoredCount = 0;
        this.RunSelectionBatch(
            () =>
            {
                selection.ClearSelection();
                reshape();

                var indices = this.ShownIndicesOf(selected);
                restoredCount = indices.Length;
                if (indices.Length > 0)
                {
                    if (selection is MultipleSelectionModel<ITreeItem> restoredMulti)
                    {
                        restoredMulti.SelectItemsAt(indices);
                    }
                    else
                    {
                        selection.SelectItemAt(indices[0]);
                    }
                }

                this.ForgetActiveItemIfDeselected();
                this.ForgetHiddenAnchor();
            },

            // Rows that only moved keep the same selection and active item: nothing to publish.
            raiseSettled: () => restoredCount != selected.Count || !ReferenceEquals(this.ActiveItem, activeBefore));
    }

    /// <summary>
    ///     Maps <paramref name="items" /> to their shown positions in one pass over the shown rows.
    /// </summary>
    /// <param name="items">The items to locate; items that are not shown are skipped.</param>
    /// <returns>The shown positions, in the order of <paramref name="items" />.</returns>
    private int[] ShownIndicesOf(IEnumerable<ITreeItem> items)
    {
        var positions = new Dictionary<ITreeItem, int>(this.shownItems.Count, ReferenceEqualityComparer.Instance);
        for (var index = 0; index < this.shownItems.Count; index++)
        {
            positions[this.shownItems[index]] = index;
        }

        var indices = new List<int>();
        foreach (var item in items)
        {
            if (positions.TryGetValue(item, out var position))
            {
                indices.Add(position);
            }
        }

        return [.. indices];
    }

    /// <summary>
    ///     Adds <paramref name="items" /> to the selection in one operation; the last shown one becomes
    ///     the selected item.
    /// </summary>
    private void SelectAdditionally(MultipleSelectionModel<ITreeItem> selection, IEnumerable<ITreeItem> items)
    {
        var indices = this.ShownIndicesOf(items);
        if (indices.Length > 0)
        {
            selection.SelectItemsAt([.. selection.SelectedIndices, .. indices]);
        }
    }

    /// <summary>
    ///     Inverts the selection among the included, shown rows of <paramref name="candidates" /> in one operation.
    /// </summary>
    private void InvertAmong(MultipleSelectionModel<ITreeItem> selection, IEnumerable<ITreeItem> candidates)
    {
        this.ClearExcludedFromSelection();

        var included = this.ShownIndicesOf(candidates.Where(this.IsIncludedInBulk));
        var includedSet = included.ToHashSet();
        var selectedSet = selection.SelectedIndices.ToHashSet();

        var result = new List<int>();
        result.AddRange(selection.SelectedIndices.Where(index => !includedSet.Contains(index)));
        result.AddRange(included.Where(index => !selectedSet.Contains(index)));

        ReplaceSelection(selection, result);
        this.ForgetActiveItemIfDeselected();
    }

    /// <summary>
    ///     The selection body shared by the pointer, keyboard and command paths, run inside one transaction:
    ///     the active identity is written before the model mutates, so the notification settled at the end
    ///     carries the final membership and primary together.
    /// </summary>
    /// <param name="item">The clicked or targeted item.</param>
    /// <param name="candidates">The rows a range may cover, in display order.</param>
    /// <param name="isControlDown">Whether the gesture toggles or extends instead of replacing.</param>
    /// <param name="isShiftDown">Whether the gesture selects a range from the anchor.</param>
    private void ApplySelection(ITreeItem item, IReadOnlyList<ITreeItem> candidates, bool isControlDown, bool isShiftDown)
    {
        if (this.SelectionModel is not { } selection)
        {
            return;
        }

        if (selection is not MultipleSelectionModel<ITreeItem> multi || !(isShiftDown || isControlDown))
        {
            this.SelectOnly(selection, item);
        }
        else if (isShiftDown)
        {
            this.SelectRangeTo(multi, item, candidates, keepExisting: isControlDown);
        }
        else
        {
            this.ToggleSelected(multi, item);
        }
    }

    /// <summary>
    ///     Makes <paramref name="item" /> the whole selection, the active identity and the range anchor.
    /// </summary>
    private void SelectOnly(SelectionModel<ITreeItem> selection, ITreeItem item)
    {
        this.SetActiveItem(item);

        // Selecting into an empty model emits an Add instead of the Reset a replace batches into.
        if (selection.IsEmpty)
        {
            selection.SelectItem(item);
        }
        else
        {
            selection.ClearAndSelectItem(item);
        }

        this.displayedSelectionAnchor = item;
    }

    /// <summary>
    ///     Toggles <paramref name="item" /> in the selection (the Ctrl gesture).
    /// </summary>
    private void ToggleSelected(MultipleSelectionModel<ITreeItem> selection, ITreeItem item)
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
            if (this.IsCoSelectionAllowed(item))
            {
                selection.SelectItem(item);
            }
            else
            {
                selection.ClearAndSelectItem(item);
            }
        }

        this.displayedSelectionAnchor = item;
    }

    /// <summary>
    ///     Selects the included rows between the range anchor and <paramref name="item" />, which becomes the
    ///     selected item and the active identity.
    /// </summary>
    private void SelectRangeTo(MultipleSelectionModel<ITreeItem> selection, ITreeItem item, IReadOnlyList<ITreeItem> candidates, bool keepExisting)
    {
        if (!this.IsCoSelectionAllowed(item) || !this.IsIncludedInBulk(item))
        {
            // The clicked end is policy-excluded from ranges (for example an exclusive row):
            // it becomes the whole selection instead of widening the range onto it.
            this.SelectOnly(selection, item);
            return;
        }

        var anchor = ResolveRangeAnchor();
        var first = IndexOfItem(candidates, anchor);
        var last = IndexOfItem(candidates, item);

        // The target goes last so it ends up as the selected item whatever the direction.
        var range = new List<ITreeItem>();
        for (var index = Math.Min(first, last); index <= Math.Max(first, last); index++)
        {
            var ranged = candidates[index];
            if (!ReferenceEquals(ranged, item) && this.IsIncludedInBulk(ranged))
            {
                range.Add(ranged);
            }
        }

        range.Add(item);

        this.SetActiveItem(item);
        if (keepExisting)
        {
            this.SelectAdditionally(selection, range);
        }
        else
        {
            selection.SelectItemsAt(this.ShownIndicesOf(range));
        }

        this.displayedSelectionAnchor = anchor;

        // The remembered anchor wins while it is still selected and in range; otherwise the selected item, else the target.
        ITreeItem ResolveRangeAnchor()
        {
            if (this.displayedSelectionAnchor is { IsSelected: true } remembered && IndexOfItem(candidates, remembered) >= 0)
            {
                return remembered;
            }

            return selection.SelectedItem is { } selected && IndexOfItem(candidates, selected) >= 0 ? selected : item;
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
                if (!this.IsIncludedInBulk(this.GetShownItemAt(index)))
                {
                    multi.ClearSelection(index);
                }
            }
        }

        this.ForgetActiveItemIfDeselected();
    }

    /// <summary>
    ///     Drops the active identity and the range anchor that a removal took out of the selection or the shown rows.
    /// </summary>
    private void ForgetRemovedSelectionState()
    {
        this.ForgetActiveItemIfDeselected();
        this.ForgetHiddenAnchor();
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
    ///     Drops the range anchor when its row is no longer shown.
    /// </summary>
    private void ForgetHiddenAnchor()
    {
        if (this.displayedSelectionAnchor is { } anchor && !this.shownItems.Contains(anchor))
        {
            this.displayedSelectionAnchor = null;
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

            this.PropertyChanging += (_, args) =>
            {
                var propertyName = args.PropertyName;
                if (propertyName?.Equals(nameof(this.SelectedItem), StringComparison.Ordinal) == true
                    && this.SelectedItem is not null)
                {
                    this.SelectedItem.IsSelected = false;
                }
            };

            this.PropertyChanged += (_, args) =>
            {
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
