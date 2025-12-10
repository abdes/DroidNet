// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Diagnostics;
using DroidNet.Controls.Selection;
using Microsoft.Extensions.Logging;

namespace DroidNet.Controls;

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

/// <summary>
///     Aggregates all manipulations that affect the visible items collection of the dynamic tree, keeping selection,
///     events, and visibility rules consistent across insert/remove/move operations.
/// </summary>
internal sealed class TreeDisplayHelper
{
    public const int DefaultMaxDepth = 32;

    private static int ClampIndex(int index, int max) => Math.Clamp(index, 0, max);

    private static List<ITreeItem> FlattenMoveSet(IReadOnlyList<ITreeItem> items)
    {
        var unique = new HashSet<ITreeItem>(items);
        if (unique.Count != items.Count)
        {
            throw new ArgumentException("items contains duplicates", nameof(items));
        }

        var set = new HashSet<ITreeItem>(items);
        return items.Where(item => !HasAncestorInSet(item, set)).ToList();
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

    private readonly ObservableCollection<ITreeItem> shownItems;
    private readonly Func<SelectionModel<ITreeItem>?> selectionModelProvider;
    private readonly Func<ITreeItem, Task> expandItemAsync;
    private readonly TreeDisplayEventCallbacks events;
    private readonly ILogger logger;
    private readonly int maxDepth;

    public TreeDisplayHelper(
        ObservableCollection<ITreeItem> shownItems,
        Func<SelectionModel<ITreeItem>?> selectionModelProvider,
        Func<ITreeItem, Task> expandItemAsync,
        TreeDisplayEventCallbacks events,
        ILogger logger,
        int maxDepth = DefaultMaxDepth)
    {
        this.shownItems = shownItems;
        this.selectionModelProvider = selectionModelProvider;
        this.expandItemAsync = expandItemAsync;
        this.events = events;
        this.logger = logger;
        this.maxDepth = maxDepth;
    }

    private SelectionModel<ITreeItem>? SelectionModel => this.selectionModelProvider();

    /// <summary>
    ///     Inserts an item under a parent at a relative index and updates the visible collection and selection accordingly.
    /// </summary>
    /// <param name="relativeIndex">Zero-based position among the parent's children.</param>
    /// <param name="parent">The parent that will receive the new child.</param>
    /// <param name="item">The item to insert.</param>
    /// <returns>A task that completes when the insertion is finished.</returns>
    public async Task InsertItemAsync(int relativeIndex, ITreeItem parent, ITreeItem item)
    {
        relativeIndex = ClampIndex(relativeIndex, parent.ChildrenCount);
        await this.EnsureParentExpandedAsync(parent).ConfigureAwait(true);

        if (!this.ApproveItemBeingAdded(parent, item))
        {
            return;
        }

        this.SelectionModel?.ClearSelection();
        var shownInsertIndex = await this.FindShownInsertIndexAsync(parent, relativeIndex).ConfigureAwait(true);

        await parent.InsertChildAsync(relativeIndex, item).ConfigureAwait(true);
        this.shownItems.Insert(shownInsertIndex, item);

        this.SelectionModel?.SelectItemAt(shownInsertIndex);

        this.events.ItemAdded?.Invoke(
            new TreeItemAddedEventArgs { Parent = parent, TreeItem = item, RelativeIndex = relativeIndex });
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
            this.shownItems.RemoveAt(shownIndex);
            removedShownCount++;
        }

        if (isShown && updateSelection)
        {
            this.UpdateSelectionAfterRemoval(shownIndex, removedShownCount);
        }

        this.events.ItemRemoved?.Invoke(
            new TreeItemRemovedEventArgs { Parent = parent, RelativeIndex = relativeIndex, TreeItem = item });
    }

    public async Task RemoveSelectedItemsAsync()
    {
        var selection = this.SelectionModel;
        if (selection is null || selection.IsEmpty)
        {
            return;
        }

        switch (selection)
        {
            case SingleSelectionModel<ITreeItem>:
                var selectedItem = selection.SelectedItem;
                if (selectedItem is null || selectedItem.IsLocked)
                {
                    return;
                }

                selection.ClearSelection();
                await this.RemoveItemAsync(selectedItem, updateSelection: true).ConfigureAwait(true);
                break;

            case MultipleSelectionModel<ITreeItem> multipleSelection:
                var selectedIndices = multipleSelection.SelectedIndices.OrderDescending().ToList();
                multipleSelection.ClearSelection();

                var originalShownItemsCount = this.shownItems.Count;

                var tasks = selectedIndices
                    .Select(index => this.shownItems[index])
                    .Where(item => !item.IsLocked)
                    .Select(item => this.RemoveItemAsync(item, updateSelection: false));

                foreach (var task in tasks)
                {
                    await task.ConfigureAwait(true);
                }

                var removedItemsCount = originalShownItemsCount - this.shownItems.Count;
                this.UpdateSelectionAfterRemoval(selectedIndices[0], removedItemsCount);
                break;
        }
    }

    /// <summary>
    ///     Moves a single item to a new parent at the specified index.
    /// </summary>
    /// <param name="item">The item to move.</param>
    /// <param name="newParent">The target parent. Must be shown and accept children.</param>
    /// <param name="newIndex">The desired index under the target parent.</param>
    /// <returns>A task that completes when the move operation finishes.</returns>
    public async Task MoveItemAsync(ITreeItem item, ITreeItem newParent, int newIndex)
    {
        await this.MoveItemsAsync([item], newParent, newIndex).ConfigureAwait(true);
    }

    /// <summary>
    ///     Moves multiple items to a new parent preserving their relative order and applying batch selection updates.
    /// </summary>
    /// <param name="items">The items to move. Must be shown and not duplicates.</param>
    /// <param name="newParent">The target parent for the move.</param>
    /// <param name="startIndex">The index at which the first item should be inserted.</param>
    /// <returns>A task that completes when all items are moved.</returns>
    public async Task MoveItemsAsync(IReadOnlyList<ITreeItem> items, ITreeItem newParent, int startIndex)
    {
        var plan = this.ValidateMoveRequest(items, newParent, startIndex);

        var originalIndices = await this.CaptureOriginalIndicesAsync(plan.Items).ConfigureAwait(true);
        var adjustedStartIndex = AdjustStartIndexForSameParent(originalIndices, plan.TargetParent, plan.StartIndex);
        plan = new MovePlan(plan.Items, plan.TargetParent, adjustedStartIndex);

        await this.EnsureParentExpandedAsync(plan.TargetParent).ConfigureAwait(true);

        var selectionSnapshot = CaptureSelection(plan.Items);
        this.SelectionModel?.ClearSelection();

        var moveBlocks = await this.DetachBlocksAsync(plan.Items, originalIndices).ConfigureAwait(true);
        var moves = await this.InsertBlocksAsync(plan, moveBlocks).ConfigureAwait(true);

        this.RestoreSelectionAfterMove(selectionSnapshot, plan.Items);
        this.events.ItemMoved?.Invoke(new TreeItemsMovedEventArgs { Moves = moves });
    }

    /// <summary>
    ///     Reorders an item within its current parent.
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
    ///     Reorders multiple items under their common parent.
    /// </summary>
    /// <param name="items">The items to reorder; all must share the same parent.</param>
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

    private async Task EnsureParentExpandedAsync(ITreeItem parent)
    {
        if (parent.IsExpanded)
        {
            return;
        }

        await this.expandItemAsync(parent).ConfigureAwait(true);
    }

    private bool ApproveItemBeingAdded(ITreeItem parent, ITreeItem item)
    {
        var eventArgs = new TreeItemBeingAddedEventArgs { Parent = parent, TreeItem = item };
        this.events.ItemBeingAdded?.Invoke(eventArgs);
        return eventArgs.Proceed;
    }

    private bool ApproveItemBeingRemoved(ITreeItem item)
    {
        var eventArgs = new TreeItemBeingRemovedEventArgs { TreeItem = item };
        this.events.ItemBeingRemoved?.Invoke(eventArgs);
        return eventArgs.Proceed;
    }

    private bool ApproveItemBeingMoved(TreeItemBeingMovedEventArgs args)
    {
        var handler = this.events.ItemBeingMoved;
        return handler is null || handler(args);
    }

    private MovePlan ValidateMoveRequest(IReadOnlyList<ITreeItem> items, ITreeItem newParent, int startIndex)
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
                throw new InvalidOperationException(args.VetoReason ?? "move vetoed");
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

    private static SelectionSnapshot CaptureSelection(IReadOnlyList<ITreeItem> items)
    {
        var firstItemSelected = items.Count > 0 && items[0].IsSelected;
        return new SelectionSnapshot(firstItemSelected);
    }

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
            await previousParent.RemoveChildAsync(item).ConfigureAwait(true);
            blocks[item] = new MoveBlock(block, previousParent, previousIndex);
        }

        return blocks;
    }

    private async Task<IReadOnlyList<TreeItemsMovedEventArgs.MovedItemInfo>> InsertBlocksAsync(
        MovePlan plan,
        IReadOnlyDictionary<ITreeItem, MoveBlock> blocks)
    {
        var moves = new List<TreeItemsMovedEventArgs.MovedItemInfo>(plan.Items.Count);
        var insertOffset = 0;

        foreach (var item in plan.Items)
        {
            var targetIndex = ClampIndex(plan.StartIndex + insertOffset, plan.TargetParent.ChildrenCount);
            var shownInsertIndex = await this.FindShownInsertIndexAsync(plan.TargetParent, targetIndex).ConfigureAwait(true);

            await plan.TargetParent.InsertChildAsync(targetIndex, item).ConfigureAwait(true);

            var block = blocks[item].ShownItems;
            for (var i = 0; i < block.Count; i++)
            {
                this.shownItems.Insert(shownInsertIndex + i, block[i]);
            }

            moves.Add(
                new TreeItemsMovedEventArgs.MovedItemInfo(
                    item,
                    blocks[item].PreviousParent,
                    plan.TargetParent,
                    blocks[item].PreviousIndex,
                    targetIndex));

            insertOffset++;
        }

        return moves;
    }

    private void RestoreSelectionAfterMove(SelectionSnapshot selectionSnapshot, IReadOnlyList<ITreeItem> movedItems)
    {
        if (!selectionSnapshot.FirstItemSelected)
        {
            return;
        }

        var first = movedItems[0];
        var newIndex = this.shownItems.IndexOf(first);
        if (newIndex >= 0)
        {
            this.SelectionModel?.SelectItemAt(newIndex);
        }
    }

    private static void ValidateRemovalPreconditions(ITreeItem item)
    {
        if (item.IsLocked)
        {
            throw new InvalidOperationException($"attempt to remove locked item `{item.Label}`");
        }

        if (!item.IsRoot && item.Parent is null)
        {
            throw new InvalidOperationException($"attempt to remove orphan item `{item.Label}`");
        }
    }

    private async Task<int> RemoveChildrenAsync(ITreeItem item, bool itemIsShown)
    {
        var removed = 0;
        var children = await item.Children.ConfigureAwait(true);
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
            return this.shownItems.IndexOf(parent) + 1;
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

            return insertIndex;
        }

        var nextSibling = siblings[relativeIndex];
        var nextIndex = this.shownItems.IndexOf(nextSibling);
        Debug.Assert(nextIndex >= 0, "next sibling must be shown");
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

    private void ValidateMovePreconditions(ITreeItem item, ITreeItem newParent)
    {
        this.ValidateItemBasics(item);
        this.ValidateTargetConstraints(item, newParent);
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
        var currentIndex = startIndex + 1;
        while (currentIndex < this.shownItems.Count && this.shownItems[currentIndex].Depth > parentDepth)
        {
            block.Add(this.shownItems[currentIndex]);
            currentIndex++;
        }

        for (var i = 0; i < block.Count; i++)
        {
            this.shownItems.RemoveAt(startIndex);
        }

        return block;
    }

    private async Task<Dictionary<ITreeItem, int>> CaptureOriginalIndicesAsync(IReadOnlyList<ITreeItem> items)
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

    private readonly record struct MovePlan(IReadOnlyList<ITreeItem> Items, ITreeItem TargetParent, int StartIndex);

    private readonly record struct MoveBlock(
        IReadOnlyList<ITreeItem> ShownItems,
        ITreeItem PreviousParent,
        int PreviousIndex);

    private readonly record struct SelectionSnapshot(bool FirstItemSelected);
}
