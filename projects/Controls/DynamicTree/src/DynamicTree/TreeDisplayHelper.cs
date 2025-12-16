// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Diagnostics;
using DroidNet.Controls.Selection;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

namespace DroidNet.Controls;

/// <summary>
///     Aggregates all manipulations that affect the visible items collection of the dynamic tree, keeping selection,
///     events, and visibility rules consistent across insert/remove/move operations.
/// </summary>
internal sealed partial class TreeDisplayHelper(
    ObservableCollection<ITreeItem> shownItems,
    Func<SelectionModel<ITreeItem>?> selectionModelProvider,
    Func<ITreeItem, Task> expandItemAsync,
    TreeDisplayHelper.TreeDisplayEventCallbacks events,
    ILoggerFactory? loggerFactory = null,
    int maxDepth = TreeDisplayHelper.DefaultMaxDepth)
{
    /// <summary>
    ///     The default maximum depth allowed for the tree.
    /// </summary>
    public const int DefaultMaxDepth = 32;

    private readonly ILogger logger = loggerFactory?.CreateLogger<TreeDisplayHelper>() ?? NullLoggerFactory.Instance.CreateLogger<TreeDisplayHelper>();
    private readonly ObservableCollection<ITreeItem> shownItems = shownItems;
    private readonly Func<SelectionModel<ITreeItem>?> selectionModelProvider = selectionModelProvider;
    private readonly Func<ITreeItem, Task> expandItemAsync = expandItemAsync;
    private readonly TreeDisplayEventCallbacks events = events;
    private readonly int maxDepth = maxDepth;

    private SelectionModel<ITreeItem>? SelectionModel => this.selectionModelProvider();

    /// <summary>
    ///     Inserts an item under a targetParent at a relative index and updates the visible collection and selection accordingly.
    /// </summary>
    /// <param name="item">The item to insert.</param>
    /// <param name="targetParent">The targetParent that will receive the new child.</param>
    /// <param name="relativeIndex">Zero-based position among the targetParent's children.</param>
    /// <returns>A task that completes when the insertion is finished.</returns>
    public async Task InsertItemAsync(ITreeItem item, ITreeItem targetParent, int relativeIndex)
    {
        this.LogInsertItemRequested(targetParent, relativeIndex, item);

        if (!this.ApproveItemBeingAdded(targetParent, item))
        {
            return;
        }

        // Clear selection first to avoid index invalidation during tree mutation
        this.SelectionModel?.ClearSelection();

        // Ensure target parent is expanded, for two things: to ensure its children collection is
        // loaded, and to make the new item visible
        await this.EnsureParentExpandedAsync(targetParent).ConfigureAwait(true);

        // Note 1: that indices must be calculated before insertion.
        // Note 2: the tree control does not react to changes to the children collection, so we must explicitly update the shown items after insertion.
        relativeIndex = this.ClampRelativeIndex(targetParent, relativeIndex);
        var treeInsertIndex = await this.FindShownInsertIndexAsync(targetParent, relativeIndex).ConfigureAwait(true);
        await targetParent.InsertChildAsync(relativeIndex, item).ConfigureAwait(true);
        this.shownItems.Insert(treeInsertIndex, item);

        this.LogShownItemsInsert(treeInsertIndex, item);

        // Select the newly added item
        this.SelectionModel?.SelectItemAt(treeInsertIndex);

        this.events.ItemAdded?.Invoke(new TreeItemAddedEventArgs
        {
            Parent = targetParent,
            TreeItem = item,
            RelativeIndex = relativeIndex,
        });
    }

    /// <summary>
    ///     Removes an item and its immediate children from the tree, updating selection if requested.
    /// </summary>
    /// <param name="item">The item to remove.</param>
    /// <param name="updateSelection">Whether selection should be updated after removal.</param>
    /// <returns>A task that completes when the removal is finished.</returns>
    public async Task RemoveItemAsync(ITreeItem item, bool updateSelection)
    {
        ValidateRemovalPreconditions(item);

        var shownIndex = this.shownItems.IndexOf(item);
        var isShown = shownIndex != -1;

        this.LogRemoveItemRequested(item);
        if (!this.ApproveItemBeingRemoved(item))
        {
            return;
        }

        if (isShown && updateSelection)
        {
            this.SelectionModel?.ClearSelection();
        }

        var removedShownCount = await this.RemoveChildrenAsync(item, isShown).ConfigureAwait(true);
        var parent = item.Parent!;
        var relativeIndex = await parent.RemoveChildAsync(item).ConfigureAwait(true);

        if (isShown)
        {
            this.LogShownItemsRemoveAt(shownIndex);
            this.shownItems.RemoveAt(shownIndex);
            removedShownCount++;
        }

        if (isShown && updateSelection)
        {
            this.UpdateSelectionAfterRemoval(shownIndex, removedShownCount);
        }

        this.events.ItemRemoved?.Invoke(
            new TreeItemRemovedEventArgs { Parent = parent, RelativeIndex = relativeIndex, TreeItem = item });

        this.LogRemoveItemCompleted(item, removedShownCount, isShown);
    }

    /// <summary>
    ///     Removes all selected items from the tree, updating the selection and visible collection accordingly.
    /// </summary>
    /// <returns>A task that completes when all selected items have been removed.</returns>
    public async Task RemoveSelectedItemsAsync()
    {
        var selection = this.SelectionModel;
        if (selection?.IsEmpty != false)
        {
            return;
        }

        switch (selection)
        {
            case SingleSelectionModel<ITreeItem>:
                var selectedItem = selection.SelectedItem;
                if (selectedItem is null)
                {
                    return;
                }

                this.LogRemoveSingleSelectedItem(selectedItem);

                if (selectedItem.IsLocked)
                {
                    return;
                }

                selection.ClearSelection();
                await this.RemoveItemAsync(selectedItem, updateSelection: true).ConfigureAwait(true);
                break;

            case MultipleSelectionModel<ITreeItem> multipleSelection:
                var selectedIndices = multipleSelection.SelectedIndices.OrderDescending().ToList();

                // notify start
                this.LogRemoveMultipleSelectionItemsStarted(selectedIndices.Count);

                multipleSelection.ClearSelection();

                var removedCount = 0;
                foreach (var index in selectedIndices)
                {
                    var item = this.shownItems[index];
                    if (item.IsLocked)
                    {
                        continue;
                    }

                    await this.RemoveItemAsync(item, updateSelection: false).ConfigureAwait(true);
                    removedCount++;
                }

                this.UpdateSelectionAfterRemoval(selectedIndices[0], removedCount);
                this.LogRemoveMultipleSelectionItemsCompleted(removedCount);
                break;
        }
    }

    /// <summary>
    ///     Moves a single item to a new targetParent at the specified index.
    /// </summary>
    /// <param name="item">The item to move.</param>
    /// <param name="newParent">The target targetParent. Must be shown and accept children.</param>
    /// <param name="newIndex">The desired index under the target targetParent.</param>
    /// <returns>A task that completes when the move operation finishes.</returns>
    public async Task MoveItemAsync(ITreeItem item, ITreeItem newParent, int newIndex)
        => await this.MoveItemsAsync([item], newParent, newIndex).ConfigureAwait(true);

    /// <summary>
    ///     Moves multiple items to a new targetParent preserving their relative order and applying batch selection updates.
    /// </summary>
    /// <param name="items">The items to move. Must be shown and not duplicates.</param>
    /// <param name="newParent">The target targetParent for the move.</param>
    /// <param name="startIndex">The index at which the first item should be inserted.</param>
    /// <returns>A task that completes when all items are moved.</returns>
    public async Task MoveItemsAsync(IReadOnlyList<ITreeItem> items, ITreeItem newParent, int startIndex)
    {
        var planNullable = this.ValidateMoveRequest(items, newParent, startIndex);
        if (planNullable is null)
        {
            // Move vetoed by handler; return gracefully without any mutation.
            this.LogMoveRequestVetoed();
            return;
        }

        var plan = planNullable.Value;

        var originalIndices = await CaptureOriginalIndicesAsync(plan.Items).ConfigureAwait(true);
        var adjustedStartIndex = AdjustStartIndexForSameParent(originalIndices, plan.TargetParent, plan.StartIndex);
        plan = new MovePlan(plan.Items, plan.TargetParent, adjustedStartIndex);

        var selectionSnapshot = this.CaptureSelection(plan.Items);
        await this.EnsureParentExpandedAsync(plan.TargetParent).ConfigureAwait(true);

        this.SelectionModel?.ClearSelection();

        var moveBlocks = await this.DetachBlocksAsync(plan.Items, originalIndices).ConfigureAwait(true);
        var moves = await this.InsertBlocksAsync(plan, moveBlocks).ConfigureAwait(true);

        this.RestoreSelectionAfterMove(selectionSnapshot);
        this.events.ItemMoved?.Invoke(new TreeItemsMovedEventArgs { Moves = moves });
    }

    /// <summary>
    ///     Reorders an item within its current targetParent.
    /// </summary>
    /// <param name="item">The item to reorder.</param>
    /// <param name="newIndex">The target sibling index.</param>
    /// <returns>A task that completes when the reorder is finished.</returns>
    public async Task ReorderItemAsync(ITreeItem item, int newIndex)
    {
        var parent = item.Parent ?? throw new InvalidOperationException("cannot reorder the root item");
        await this.MoveItemAsync(item, parent, newIndex).ConfigureAwait(true);
    }

    /// <summary>
    ///     Reorders multiple items under their common targetParent.
    /// </summary>
    /// <param name="items">The items to reorder; all must share the same targetParent.</param>
    /// <param name="startIndex">The index of the first item after reorder.</param>
    /// <returns>A task that completes when the reorder is finished.</returns>
    public async Task ReorderItemsAsync(IReadOnlyList<ITreeItem> items, int startIndex)
    {
        if (items.Count == 0)
        {
            return;
        }

        var parent = items[0].Parent ?? throw new InvalidOperationException("cannot reorder items without a parent");
        if (items.Any(i => i.Parent != parent))
        {
            throw new InvalidOperationException("all items must share the same parent to reorder");
        }

        await this.MoveItemsAsync(items, parent, startIndex).ConfigureAwait(true);
    }

    private static List<ITreeItem> FlattenMoveSet(IReadOnlyList<ITreeItem> items)
    {
        var unique = new HashSet<ITreeItem>(items);
        if (unique.Count != items.Count)
        {
            throw new ArgumentException("items contains duplicates", nameof(items));
        }

        var set = new HashSet<ITreeItem>(items);
        return [.. items.Where(item => !HasAncestorInSet(item, set))];
    }

    private static bool HasAncestorInSet(ITreeItem item, HashSet<ITreeItem> set)
    {
        var current = item.Parent;
        while (current is not null)
        {
            if (set.Contains(current))
            {
                return true;
            }

            current = current.Parent;
        }

        return false;
    }

    private static bool IsDescendantOf(ITreeItem candidate, ITreeItem ancestor)
    {
        var current = candidate.Parent;
        while (current is not null)
        {
            if (ReferenceEquals(current, ancestor))
            {
                return true;
            }

            current = current.Parent;
        }

        return false;
    }

    private static void ValidateRemovalPreconditions(ITreeItem item)
    {
        if (item.IsLocked)
        {
            throw new InvalidOperationException($"attempt to remove locked item `{item.Label}`");
        }

        if (item.IsRoot)
        {
            throw new InvalidOperationException($"attempt to remove the root item `{item.Label}`");
        }

        if (item.Parent is null)
        {
            throw new InvalidOperationException($"attempt to remove orphan item `{item.Label}`");
        }
    }

    private static async Task<Dictionary<ITreeItem, int>> CaptureOriginalIndicesAsync(IReadOnlyList<ITreeItem> items)
    {
        var indices = new Dictionary<ITreeItem, int>(items.Count);
        foreach (var item in items)
        {
            var parent = item.Parent ?? throw new InvalidOperationException("item must have a parent to be moved");
            var children = await parent.Children.ConfigureAwait(true);
            var index = children.IndexOf(item);
            if (index == -1)
            {
                throw new InvalidOperationException("item was not found in its parent's children");
            }

            indices[item] = index;
        }

        return indices;
    }

    private static int AdjustStartIndexForSameParent(
        IReadOnlyDictionary<ITreeItem, int> originalIndices,
        ITreeItem targetParent,
        int startIndex)
    {
        var adjustment = originalIndices.Count(kvp => ReferenceEquals(kvp.Key.Parent, targetParent) && kvp.Value < startIndex);
        var adjusted = startIndex - adjustment;
        return adjusted < 0 ? 0 : adjusted;
    }

    private async Task EnsureParentExpandedAsync(ITreeItem parent)
    {
        if (parent.IsExpanded)
        {
            return;
        }

        // log and auto-expand targetParent to allow child operations
        this.LogParentAutoExpanded(parent);

        await this.expandItemAsync(parent).ConfigureAwait(true);
    }

    private bool ApproveItemBeingAdded(ITreeItem parent, ITreeItem item)
    {
        var eventArgs = new TreeItemBeingAddedEventArgs { Parent = parent, TreeItem = item };
        _ = this.events.ItemBeingAdded?.Invoke(eventArgs);
        var proceed = eventArgs.Proceed;
        this.LogItemBeingAddedDecision(parent, item, proceed);
        return proceed;
    }

    private bool ApproveItemBeingRemoved(ITreeItem item)
    {
        var eventArgs = new TreeItemBeingRemovedEventArgs { TreeItem = item };
        _ = this.events.ItemBeingRemoved?.Invoke(eventArgs);
        var proceed = eventArgs.Proceed;
        this.LogItemBeingRemovedDecision(item, proceed);
        return proceed;
    }

    private bool ApproveItemBeingMoved(TreeItemBeingMovedEventArgs args)
    {
        var handler = this.events.ItemBeingMoved;
        return handler is null || handler(args);
    }

    private MovePlan? ValidateMoveRequest(IReadOnlyList<ITreeItem> items, ITreeItem newParent, int startIndex)
    {
        if (items.Count == 0)
        {
            throw new ArgumentException("items cannot be empty", nameof(items));
        }

        var flattened = FlattenMoveSet(items);
        ITreeItem? resolvedParent = null;
        var resolvedIndex = startIndex;

        foreach (var item in flattened)
        {
            this.ValidateItemBasics(item);

            var currentTarget = resolvedParent ?? newParent;
            var args = new TreeItemBeingMovedEventArgs
            {
                TreeItem = item,
                PreviousParent = item.Parent!,
                NewParent = currentTarget,
                NewIndex = resolvedIndex,
                Proceed = true,
            };

            if (!this.ApproveItemBeingMoved(args))
            {
                // If a handler vetoes the move, we should return null to indicate the move was rejected
                // and allow caller to gracefully bail out. Log for debugging and diagnostics.
                this.LogMoveVetoed(args.VetoReason);
                return null;
            }

            this.ValidateTargetConstraints(item, args.NewParent);

            if (resolvedParent is null)
            {
                resolvedParent = args.NewParent;
                resolvedIndex = args.NewIndex;
                continue;
            }

            if (!ReferenceEquals(resolvedParent, args.NewParent))
            {
                throw new InvalidOperationException("all items must share the same target parent");
            }

            resolvedIndex = args.NewIndex;
        }

        Debug.Assert(resolvedParent is not null, "resolved parent must be set");
        return new MovePlan(flattened, resolvedParent!, resolvedIndex);
    }

    private SelectionSnapshot CaptureSelection(IReadOnlyList<ITreeItem> movedItems)
    {
        var selection = this.SelectionModel;
        if (selection?.IsEmpty != false || movedItems.Count == 0)
        {
            return SelectionSnapshot.Empty;
        }

        List<ITreeItem> selectedItems;
        switch (selection)
        {
            case MultipleSelectionModel<ITreeItem> multipleSelection:
                selectedItems = new List<ITreeItem>(multipleSelection.SelectedIndices.Count);
                foreach (var index in multipleSelection.SelectedIndices)
                {
                    var item = this.GetShownItemAt(index);
                    if (item is not null)
                    {
                        selectedItems.Add(item);
                    }
                }

                break;
            default:
                var selectedItem = selection.SelectedItem;
                if (selectedItem is null)
                {
                    return SelectionSnapshot.Empty;
                }

                selectedItems = [selectedItem];
                break;
        }

        var firstMoved = movedItems[0];
        var firstMovedIndex = this.shownItems.IndexOf(firstMoved);
        var firstMovedSelected = firstMovedIndex >= 0 && selection.IsSelected(firstMovedIndex);

        return new SelectionSnapshot(selectedItems, firstMoved, firstMovedSelected);
    }

    private ITreeItem? GetShownItemAt(int index)
        => index < 0 || index >= this.shownItems.Count
            ? null
            : this.shownItems[index];

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Performance", "CA1859:Use concrete types when possible for improved performance", Justification = "IReadOnly is stronger than using concrete types that are not provided by every collection")]
    private async Task<Dictionary<ITreeItem, MoveBlock>> DetachBlocksAsync(
        IReadOnlyList<ITreeItem> items,
        IReadOnlyDictionary<ITreeItem, int> originalIndices)
    {
        var blocks = new Dictionary<ITreeItem, MoveBlock>(items.Count);
        foreach (var item in items)
        {
            var block = this.ExtractShownBlock(item);
            var previousParent = item.Parent!;
            var previousIndex = originalIndices[item];
            _ = await previousParent.RemoveChildAsync(item).ConfigureAwait(true);
            blocks[item] = new MoveBlock(block, previousParent, previousIndex);
        }

        return blocks;
    }

    private async Task<IReadOnlyList<MovedItemInfo>> InsertBlocksAsync(
        MovePlan plan,
        IReadOnlyDictionary<ITreeItem, MoveBlock> blocks)
    {
        var moves = new List<MovedItemInfo>(plan.Items.Count);
        var insertOffset = 0;

        foreach (var item in plan.Items)
        {
            var targetIndex = this.ClampRelativeIndex(plan.TargetParent, plan.StartIndex + insertOffset);
            var shownInsertIndex = await this.FindShownInsertIndexAsync(plan.TargetParent, targetIndex).ConfigureAwait(true);

            await plan.TargetParent.InsertChildAsync(targetIndex, item).ConfigureAwait(true);

            var block = blocks[item].ShownItems;
            for (var i = 0; i < block.Count; i++)
            {
                this.LogShownItemsInsert(shownInsertIndex + i, block[i]);
                this.shownItems.Insert(shownInsertIndex + i, block[i]);
            }

            moves.Add(
                new MovedItemInfo(
                    item,
                    blocks[item].PreviousParent,
                    plan.TargetParent,
                    blocks[item].PreviousIndex,
                    targetIndex));

            insertOffset++;
        }

        return moves;
    }

    private void RestoreSelectionAfterMove(SelectionSnapshot selectionSnapshot)
    {
        var selection = this.SelectionModel;
        if (selection is null || selectionSnapshot.IsEmpty)
        {
            return;
        }

        if (!selectionSnapshot.FirstMovedItemSelected)
        {
            selection.ClearSelection();
            return;
        }

        var selectedIndices = new List<int>(selectionSnapshot.SelectedItems.Count);
        var firstMovedIndex = -1;

        foreach (var item in selectionSnapshot.SelectedItems)
        {
            var newIndex = this.shownItems.IndexOf(item);
            if (newIndex < 0)
            {
                continue;
            }

            if (ReferenceEquals(item, selectionSnapshot.FirstMovedItem))
            {
                firstMovedIndex = newIndex;
            }
            else
            {
                selectedIndices.Add(newIndex);
            }
        }

        if (firstMovedIndex == -1)
        {
            selection.ClearSelection();
            return;
        }

        switch (selection)
        {
            case MultipleSelectionModel<ITreeItem> multipleSelection:
                // Ensure the first moved item becomes the SelectedItem by passing it last.
                selectedIndices.Add(firstMovedIndex);
                multipleSelection.SelectItemsAt([.. selectedIndices]);
                break;
            default:
                selection.SelectItemAt(firstMovedIndex);
                break;
        }
    }

    private async Task<int> RemoveChildrenAsync(ITreeItem item, bool itemIsShown)
    {
        var removed = 0;
        var children = await item.Children.ConfigureAwait(true);
        this.LogRemoveChildrenStarted(children.Count, itemIsShown, item.IsExpanded);
        foreach (var child in children.Reverse())
        {
            if (itemIsShown && item.IsExpanded)
            {
                var block = this.ExtractShownBlock(child);
                removed += block.Count;
            }

            var childIndex = await item.RemoveChildAsync(child).ConfigureAwait(true);
            this.events.ItemRemoved?.Invoke(
                new TreeItemRemovedEventArgs { Parent = item, RelativeIndex = childIndex, TreeItem = child });
        }

        return removed;
    }

    private async Task<int> FindShownInsertIndexAsync(ITreeItem parent, int relativeIndex)
    {
        if (relativeIndex == 0)
        {
            var index = this.shownItems.IndexOf(parent) + 1;
            this.LogFindNewItemIndexComputed(parent, relativeIndex, index, "none", sibling: null);
            return index;
        }

        var siblings = await parent.Children.ConfigureAwait(true);
        if (relativeIndex == parent.ChildrenCount)
        {
            var sibling = siblings[relativeIndex - 1];
            var siblingIndex = this.shownItems.IndexOf(sibling);
            Debug.Assert(siblingIndex >= 0, "sibling must be shown");

            var insertIndex = siblingIndex + 1;
            while (insertIndex < this.shownItems.Count && this.shownItems[insertIndex].Depth > sibling.Depth)
            {
                insertIndex++;
            }

            this.LogFindNewItemIndexComputed(parent, relativeIndex, insertIndex, "sibling", sibling);
            return insertIndex;
        }

        var nextSibling = siblings[relativeIndex];
        var nextIndex = this.shownItems.IndexOf(nextSibling);
        Debug.Assert(nextIndex >= 0, "next sibling must be shown");
        this.LogFindNewItemIndexComputed(parent, relativeIndex, nextIndex, "next", nextSibling);
        return nextIndex;
    }

    private void UpdateSelectionAfterRemoval(int lastSelectedIndex, int removedItemsCount)
    {
        var selection = this.SelectionModel;
        if (selection is null)
        {
            return;
        }

        var newSelectedIndex = this.shownItems.Count != 0 ? 0 : -1;
        switch (selection)
        {
            case SingleSelectionModel<ITreeItem>:
                if (lastSelectedIndex < this.shownItems.Count)
                {
                    newSelectedIndex = lastSelectedIndex;
                }

                break;
            case MultipleSelectionModel<ITreeItem>:
                var tentativeIndex = lastSelectedIndex + 1 - removedItemsCount;
                if (tentativeIndex > 0 && tentativeIndex < this.shownItems.Count)
                {
                    newSelectedIndex = tentativeIndex;
                }

                break;
            default:
                return;
        }

        if (newSelectedIndex == -1)
        {
            return;
        }

        selection.SelectItemAt(newSelectedIndex);
    }

    private List<ITreeItem> ExtractShownBlock(ITreeItem item)
    {
        var startIndex = this.shownItems.IndexOf(item);
        if (startIndex == -1)
        {
            throw new InvalidOperationException("item must be shown to be moved");
        }

        var block = new List<ITreeItem> { item };
        var parentDepth = item.Depth;
        for (var currentIndex = startIndex + 1;
            currentIndex < this.shownItems.Count && this.shownItems[currentIndex].Depth > parentDepth;
            currentIndex++)
        {
            block.Add(this.shownItems[currentIndex]);
        }

        for (var i = 0; i < block.Count; i++)
        {
            this.LogShownItemsRemoveAt(startIndex);
            this.shownItems.RemoveAt(startIndex);
        }

        return block;
    }

    private void ValidateItemBasics(ITreeItem item)
    {
        if (!this.shownItems.Contains(item))
        {
            throw new InvalidOperationException("item must be shown before it can be moved");
        }

        if (item.IsRoot)
        {
            throw new InvalidOperationException("cannot move the root item");
        }

        if (item.IsLocked)
        {
            throw new InvalidOperationException("cannot move a locked item");
        }

        if (item.Parent is null)
        {
            throw new InvalidOperationException("item must have a parent to be moved");
        }
    }

    private void ValidateTargetConstraints(ITreeItem item, ITreeItem targetParent)
    {
        if (!this.shownItems.Contains(targetParent))
        {
            throw new InvalidOperationException("target parent must be shown");
        }

        if (!targetParent.CanAcceptChildren)
        {
            throw new InvalidOperationException("target parent does not accept children");
        }

        if (ReferenceEquals(item, targetParent) || IsDescendantOf(targetParent, item))
        {
            throw new InvalidOperationException("cannot move an item into its descendant");
        }

        if (targetParent.Depth + 1 > this.maxDepth)
        {
            throw new InvalidOperationException("move would exceed maximum depth");
        }
    }

    private int ClampRelativeIndex(ITreeItem parent, int relativeIndex)
    {
        var clamped = Math.Clamp(relativeIndex, 0, parent.ChildrenCount);
        if (clamped != relativeIndex)
        {
            this.LogRelativeIndexAdjusted(parent, relativeIndex, clamped);
        }

        return clamped;
    }

    /// <summary>
    ///     Event callbacks used by <see cref="TreeDisplayHelper" /> to surface existing ViewModel events without direct
    ///     coupling.
    /// </summary>
    internal readonly record struct TreeDisplayEventCallbacks(
        Func<TreeItemBeingAddedEventArgs, bool>? ItemBeingAdded,
        Action<TreeItemAddedEventArgs>? ItemAdded,
        Func<TreeItemBeingRemovedEventArgs, bool>? ItemBeingRemoved,
        Action<TreeItemRemovedEventArgs>? ItemRemoved,
        Func<TreeItemBeingMovedEventArgs, bool>? ItemBeingMoved,
        Action<TreeItemsMovedEventArgs>? ItemMoved);

    private readonly record struct MovePlan(IReadOnlyList<ITreeItem> Items, ITreeItem TargetParent, int StartIndex);

    private readonly record struct MoveBlock(
        IReadOnlyList<ITreeItem> ShownItems,
        ITreeItem PreviousParent,
        int PreviousIndex);

    private readonly record struct SelectionSnapshot(IReadOnlyList<ITreeItem> SelectedItems, ITreeItem? FirstMovedItem, bool FirstMovedItemSelected)
    {
        public static SelectionSnapshot Empty { get; } = new([], FirstMovedItem: null, FirstMovedItemSelected: false);

        public bool IsEmpty => this.SelectedItems.Count == 0 || this.FirstMovedItem is null;
    }
}
