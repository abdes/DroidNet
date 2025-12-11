// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace DroidNet.Controls;

/// <summary>
///     Logging helpers for <see cref="Expander"/>.
/// </summary>
public partial class DynamicTreeViewModel
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Cannot expand item '{item}': item is not root, and its parent '{parent}' is not expanded")]
    private static partial void LogExpandItemNotVisible(ILogger logger, string item, string parent);

    private void LogExpandItemNotVisible(ITreeItem item)
        => LogExpandItemNotVisible(this.logger, item.Label, item.Parent?.Label ?? "<null>");

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Expanding item '{item}': Depth={depth}, Children={childrenCount}")]
    private static partial void LogExpandItem(ILogger logger, string item, int depth, int childrenCount);

    [Conditional("DEBUG")]
    private void LogExpandItem(ITreeItem item)
        => LogExpandItem(this.logger, item.Label, item.Depth, item.ChildrenCount);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Initializing root '{root}' (skipRoot={skipRoot}, IsExpanded={isExpanded})")]
    private static partial void LogInitializeRoot(ILogger logger, string root, bool skipRoot, bool isExpanded);

    private void LogInitializeRoot(ITreeItem root, bool skipRoot)
        => LogInitializeRoot(this.logger, root.Label, skipRoot, root.IsExpanded);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Auto-expanding parent '{parent}' to allow child insertion (wasExpanded={wasExpanded})")]
    private static partial void LogParentAutoExpanded(ILogger logger, string parent, bool wasExpanded);

    [Conditional("DEBUG")]
    private void LogParentAutoExpanded(ITreeItem parent)
        => LogParentAutoExpanded(this.logger, parent.Label, parent.IsExpanded);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Item being added (approval decision) to parent '{parent}': item='{item}', proceed={proceed}")]
    private static partial void LogItemBeingAddedDecision(ILogger logger, string parent, string item, bool proceed);

    [Conditional("DEBUG")]
    private void LogItemBeingAddedDecision(ITreeItem parent, ITreeItem item, bool proceed)
        => LogItemBeingAddedDecision(this.logger, parent.Label, item.Label, proceed);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Computed insertion index: {index} (From relative={relativeIndex}) (Parent='{parent}'/depth={parentDepth}/children={childrenCount})")]
    private static partial void LogFindNewItemIndexComputedNoSibling(
        ILogger logger,
        string parent,
        int parentDepth,
        int relativeIndex,
        int index,
        int childrenCount);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Computed insertion index: {index} (From relative={relativeIndex}) (Parent='{parent}'/depth={parentDepth}/children={childrenCount}) (Sibling={siblingRole}:'{siblingLabel}'/depth={siblingDepth})")]
    private static partial void LogFindNewItemIndexComputedWithSibling(
        ILogger logger,
        string parent,
        int parentDepth,
        int relativeIndex,
        int index,
        int childrenCount,
        string siblingRole,
        string siblingLabel,
        int siblingDepth);

    [Conditional("DEBUG")]
    private void LogFindNewItemIndexComputed(ITreeItem parent, int relativeIndex, int index, string siblingRole, ITreeItem? sibling)
    {
        if (sibling is null)
        {
            LogFindNewItemIndexComputedNoSibling(this.logger, parent.Label, parent.Depth, relativeIndex, index, parent.ChildrenCount);
            return;
        }

        LogFindNewItemIndexComputedWithSibling(this.logger, parent.Label, parent.Depth, relativeIndex, index, parent.ChildrenCount, siblingRole, sibling.Label, sibling.Depth);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "InsertItem requested: item='{item}' into parent '{parent}' at relativeIndex={relativeIndex} (parentDepth={parentDepth}, children={childrenCount}, parentIsExpanded={isExpanded}, shownItems={shownCount})")]
    private static partial void LogInsertItemRequested(ILogger logger, string parent, string item, int relativeIndex, int parentDepth, int childrenCount, bool isExpanded, int shownCount);

    [Conditional("DEBUG")]
    private void LogInsertItemRequested(ITreeItem parent, int relativeIndex, ITreeItem item)
        => LogInsertItemRequested(this.logger, parent.Label, item.Label, relativeIndex, parent.Depth, parent.ChildrenCount, parent.IsExpanded, this.ShownItems.Count);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "RemoveItem requested: item='{item}' (IsRoot={isRoot}, IsLocked={isLocked}) (shownItems={shownCount})")]
    private static partial void LogRemoveItemRequested(ILogger logger, string item, bool isRoot, bool isLocked, int shownCount);

    [Conditional("DEBUG")]
    private void LogRemoveItemRequested(ITreeItem item)
        => LogRemoveItemRequested(this.logger, item.Label, item.IsRoot, item.IsLocked, this.ShownItems.Count);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Cannot remove locked item '{item}'")]
    private static partial void LogRemoveLockedItem(ILogger logger, string item);

    private void LogErrorRemoveLockedItem(ITreeItem item)
        => LogRemoveLockedItem(this.logger, item.Label);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Cannot remove orphan item '{item}': item has no parent and is not root")]
    private static partial void LogRemoveOrphanItem(ILogger logger, string item);

    private void LogErrorRemoveOrphanItem(ITreeItem item)
        => LogRemoveOrphanItem(this.logger, item.Label);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Item being removed (approval decision): item='{item}', proceed={proceed}")]
    private static partial void LogItemBeingRemovedDecision(ILogger logger, string item, bool proceed);

    [Conditional("DEBUG")]
    private void LogItemBeingRemovedDecision(ITreeItem item, bool proceed)
        => LogItemBeingRemovedDecision(this.logger, item.Label, proceed);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Completed removal of item '{item}' (removedShownItems={removedCount}, wasShown={wasShown})")]
    private static partial void LogRemoveItemCompleted(ILogger logger, string item, int removedCount, bool wasShown);

    private void LogRemoveItemCompleted(ITreeItem item, int removedCount, bool wasShown)
        => LogRemoveItemCompleted(this.logger, item.Label, removedCount, wasShown);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Request to insert an item in the tree at index: {originalIndex} for parent '{parent}' was out of range and adjusted to {clamped} (valid range [0, {childrenCount}])")]
    private static partial void LogRelativeIndexAdjusted(ILogger logger, string parent, int originalIndex, int clamped, int childrenCount);

    [Conditional("DEBUG")]
    private void LogRelativeIndexAdjusted(ITreeItem parent, int originalIndex, int clamped)
    {
        if (originalIndex == clamped)
        {
            return;
        }

        LogRelativeIndexAdjusted(this.logger, parent.Label, originalIndex, clamped, parent.ChildrenCount);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "RemoveSingleSelectedItem: Processing selected item '{item}' (IsLocked={isLocked})")]
    private static partial void LogRemoveSingleSelectedItem(ILogger logger, string item, bool isLocked);

    [Conditional("DEBUG")]
    private void LogRemoveSingleSelectedItem(ITreeItem item)
        => LogRemoveSingleSelectedItem(this.logger, item.Label, item.IsLocked);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "RemoveMultipleSelectionItems: Processing {selectedCount} items (originalShownItems={shownCount})")]
    private static partial void LogRemoveMultipleSelectionItemsStarted(ILogger logger, int selectedCount, int shownCount);

    [Conditional("DEBUG")]
    private void LogRemoveMultipleSelectionItemsStarted(int selectedCount)
        => LogRemoveMultipleSelectionItemsStarted(this.logger, selectedCount, this.ShownItems.Count);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "RemoveMultipleSelectionItems completed: Removed {removedCount} items")]
    private static partial void LogRemoveMultipleSelectionItemsCompleted(ILogger logger, int removedCount);

    [Conditional("DEBUG")]
    private void LogRemoveMultipleSelectionItemsCompleted(int removedCount)
        => LogRemoveMultipleSelectionItemsCompleted(this.logger, removedCount);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "RemoveChildren: Processing {childrenCount} children (itemIsShown={itemIsShown}, itemIsExpanded={itemIsExpanded})")]
    private static partial void LogRemoveChildrenStarted(ILogger logger, int childrenCount, bool itemIsShown, bool itemIsExpanded);

    [Conditional("DEBUG")]
    private void LogRemoveChildrenStarted(int childrenCount, bool itemIsShown, bool itemIsExpanded)
        => LogRemoveChildrenStarted(this.logger, childrenCount, itemIsShown, itemIsExpanded);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "UpdateSelectionAfterRemoval: SelectionMode={selectionMode}, newSelectedIndex={newSelectedIndex}, lastSelectedIndex={lastSelectedIndex}, removedCount={removedCount}")]
    private static partial void LogUpdateSelectionAfterRemoval(ILogger logger, string selectionMode, int newSelectedIndex, int lastSelectedIndex, int removedCount);

    [Conditional("DEBUG")]
    private void LogUpdateSelectionAfterRemoval(int newSelectedIndex, int lastSelectedIndex, int removedCount)
        => LogUpdateSelectionAfterRemoval(this.logger, this.SelectionModel?.GetType().Name ?? "<null>", newSelectedIndex, lastSelectedIndex, removedCount);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "RestoreExpandedChildren: item='{item}' (childrenCount={childrenCount})")]
    private static partial void LogRestoreExpandedChildrenStarted(ILogger logger, string item, int childrenCount);

    [Conditional("DEBUG")]
    private void LogRestoreExpandedChildrenStarted(ITreeItem item)
        => LogRestoreExpandedChildrenStarted(this.logger, item.Label, item.ChildrenCount);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "HideChildren: item='{item}' (childrenCount={childrenCount})")]
    private static partial void LogHideChildrenStarted(ILogger logger, string item, int childrenCount);

    [Conditional("DEBUG")]
    private void LogHideChildrenStarted(ITreeItem item)
        => LogHideChildrenStarted(this.logger, item.Label, item.ChildrenCount);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Copy ignored: no clonable items in selection (count={selectionCount})")]
    private static partial void LogCopyIgnoredNoClonableItems(ILogger logger, int selectionCount);

    private void LogCopyIgnoredNoClonableItems(int selectionCount)
        => LogCopyIgnoredNoClonableItems(this.logger, selectionCount);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Copy skipped for item type {itemType}: does not implement {interfaceName}")]
    private static partial void LogCopySkippedNonClonableItem(ILogger logger, string itemType, string interfaceName);

    private void LogCopySkippedNonClonableItem(ITreeItem item)
        => LogCopySkippedNonClonableItem(this.logger, item.GetType().FullName ?? item.GetType().Name, nameof(ICanBeCloned));

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Paste skipped: target '{target}' is the source cut item '{source}'")]
    private static partial void LogPasteSkippedIntoSource(ILogger logger, string target, string source);

    private void LogPasteSkippedIntoSource(ITreeItem item)
        => LogPasteSkippedIntoSource(this.logger, item.Label, item.Label);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Paste skipped: target '{target}' is a descendant of cut item '{source}'")]
    private static partial void LogPasteSkippedIntoDescendant(ILogger logger, string target, string source);

    private void LogPasteSkippedIntoDescendant(ITreeItem source, ITreeItem target)
        => LogPasteSkippedIntoDescendant(this.logger, target.Label, source.Label);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "ShownItems.Add: item='{item}' (newCount={newCount})")]
    private static partial void LogShownItemsAdd(ILogger logger, string item, int newCount);

    [Conditional("DEBUG")]
    private void LogShownItemsAdd(ITreeItem item)
        => LogShownItemsAdd(this.logger, item.Label, this.ShownItems.Count + 1);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "ShownItems.Insert: index={index}, item='{item}' (newCount={newCount})")]
    private static partial void LogShownItemsInsert(ILogger logger, int index, string item, int newCount);

    [Conditional("DEBUG")]
    private void LogShownItemsInsert(int index, ITreeItem item)
        => LogShownItemsInsert(this.logger, index, item.Label, this.ShownItems.Count + 1);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "ShownItems.RemoveAt: index={index}, item='{item}' (newCount={newCount})")]
    private static partial void LogShownItemsRemoveAt(ILogger logger, int index, string item, int newCount);

    [Conditional("DEBUG")]
    private void LogShownItemsRemoveAt(int index)
    {
        var item = this.ShownItems[index];
        LogShownItemsRemoveAt(this.logger, index, item.Label, this.ShownItems.Count - 1);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "ShownItems.Clear (shownItemsCount={count})")]
    private static partial void LogShownItemsClear(ILogger logger, int count);

    [Conditional("DEBUG")]
    private void LogShownItemsClear()
        => LogShownItemsClear(this.logger, this.ShownItems.Count);
}
