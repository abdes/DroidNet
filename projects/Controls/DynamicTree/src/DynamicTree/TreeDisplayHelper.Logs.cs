// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace DroidNet.Controls;

/// <summary>
///     Logging extensions for <see cref="TreeDisplayHelper"/>.
/// </summary>
internal sealed partial class TreeDisplayHelper
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Move request was vetoed by a handler; aborting move.")]
    private static partial void LogMoveRequestVetoed(ILogger logger);

    private void LogMoveRequestVetoed()
        => LogMoveRequestVetoed(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Move vetoed: {Reason}")]
    private static partial void LogMoveVetoed(ILogger logger, string reason);

    private void LogMoveVetoed(string? reason)
        => LogMoveVetoed(this.logger, reason ?? "move vetoed");

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "InsertItem requested: item='{item}' into parent '{parent}' at relativeIndex={relativeIndex} (parentDepth={parentDepth}, children={childrenCount}, parentIsExpanded={isExpanded}, shownItems={shownCount})")]
    private static partial void LogInsertItemRequested(ILogger logger, string parent, string item, int relativeIndex, int parentDepth, int childrenCount, bool isExpanded, int shownCount);

    private void LogInsertItemRequested(ITreeItem parent, int relativeIndex, ITreeItem item)
        => LogInsertItemRequested(this.logger, parent.Label, item.Label, relativeIndex, parent.Depth, parent.ChildrenCount, parent.IsExpanded, this.shownItems.Count);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "RemoveItem requested: item='{item}' (IsRoot={isRoot}, IsLocked={isLocked}) (shownItems={shownCount})")]
    private static partial void LogRemoveItemRequested(ILogger logger, string item, bool isRoot, bool isLocked, int shownCount);

    private void LogRemoveItemRequested(ITreeItem item)
        => LogRemoveItemRequested(this.logger, item.Label, item.IsRoot, item.IsLocked, this.shownItems.Count);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Completed removal of item '{item}' (removedShownItems={removedCount}, wasShown={wasShown})")]
    private static partial void LogRemoveItemCompleted(ILogger logger, string item, int removedCount, bool wasShown);

    private void LogRemoveItemCompleted(ITreeItem item, int removedCount, bool wasShown)
        => LogRemoveItemCompleted(this.logger, item.Label, removedCount, wasShown);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Relative iundex {originalIndex} for parent '{parent}' was out of range and adjusted to {clamped} (valid range [0, {childrenCount}])")]
    private static partial void LogRelativeIndexAdjusted(ILogger logger, string parent, int originalIndex, int clamped, int childrenCount);

    private void LogRelativeIndexAdjusted(ITreeItem parent, int originalIndex, int clamped)
        => LogRelativeIndexAdjusted(this.logger, parent.Label, originalIndex, clamped, parent.ChildrenCount);

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
        Level = LogLevel.Trace,
        Message = "RemoveChildren: Processing {childrenCount} children (itemIsShown={itemIsShown}, itemIsExpanded={itemIsExpanded})")]
    private static partial void LogRemoveChildrenStarted(ILogger logger, int childrenCount, bool itemIsShown, bool itemIsExpanded);

    private void LogRemoveChildrenStarted(int childrenCount, bool itemIsShown, bool itemIsExpanded)
        => LogRemoveChildrenStarted(this.logger, childrenCount, itemIsShown, itemIsExpanded);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "ShownItems.Insert: index={index}, item='{item}' (newCount={newCount})")]
    private static partial void LogShownItemsInsert(ILogger logger, int index, string item, int newCount);

    [Conditional("DEBUG")]
    private void LogShownItemsInsert(int index, ITreeItem item)
        => LogShownItemsInsert(this.logger, index, item.Label, this.shownItems.Count + 1);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "ShownItems.RemoveAt: index={index}, item='{item}' (newCount={newCount})")]
    private static partial void LogShownItemsRemoveAt(ILogger logger, int index, string item, int newCount);

    [Conditional("DEBUG")]
    private void LogShownItemsRemoveAt(int index)
    {
        var item = this.shownItems[index];
        LogShownItemsRemoveAt(this.logger, index, item.Label, this.shownItems.Count - 1);
    }

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
        Level = LogLevel.Debug,
        Message = "Item being removed (approval decision): item='{item}', proceed={proceed}")]
    private static partial void LogItemBeingRemovedDecision(ILogger logger, string item, bool proceed);

    [Conditional("DEBUG")]
    private void LogItemBeingRemovedDecision(ITreeItem item, bool proceed)
        => LogItemBeingRemovedDecision(this.logger, item.Label, proceed);

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
        => LogRemoveMultipleSelectionItemsStarted(this.logger, selectedCount, this.shownItems.Count);

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
        Message = "Auto-expanding parent '{parent}' to allow child insertion (wasExpanded={wasExpanded})")]
    private static partial void LogParentAutoExpanded(ILogger logger, string parent, bool wasExpanded);

    [Conditional("DEBUG")]
    private void LogParentAutoExpanded(ITreeItem parent)
        => LogParentAutoExpanded(this.logger, parent.Label, parent.IsExpanded);
}
