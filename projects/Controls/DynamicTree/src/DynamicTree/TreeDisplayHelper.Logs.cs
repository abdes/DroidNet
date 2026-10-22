// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace DroidNet.Controls;

/// <summary>
///     Logging helpers for <see cref="TreeDisplayHelper"/>.
/// </summary>
internal sealed partial class TreeDisplayHelper
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Move request was vetoed by a handler; aborting move.")]
    private static partial void LogMoveRequestVetoed(ILogger logger);

    [Conditional("DEBUG")]
    private void LogMoveRequestVetoed()
        => LogMoveRequestVetoed(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Move vetoed: {Reason}")]
    private static partial void LogMoveVetoed(ILogger logger, string reason);

    private void LogMoveVetoed(string? reason)
    {
        if (!this.logger.IsEnabled(LogLevel.Warning))
        {
            return;
        }

        var shown = reason ?? "move vetoed";
        LogMoveVetoed(this.logger, shown);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "InsertItem requested: item='{item}' into parent '{parent}' at relativeIndex={relativeIndex} (parentDepth={parentDepth}, children={childrenCount}, parentIsExpanded={isExpanded}, shownItems={shownCount})")]
    private static partial void LogInsertItemRequested(ILogger logger, string parent, string item, int relativeIndex, int parentDepth, int childrenCount, bool isExpanded, int shownCount);

    [Conditional("DEBUG")]
    private void LogInsertItemRequested(ITreeItem parent, int relativeIndex, ITreeItem item)
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        var parentLabel = parent.Label;
        var itemLabel = item.Label;
        var parentDepth = parent.Depth;
        var childrenCount = parent.ChildrenCount;
        var isExpanded = parent.IsExpanded;
        var shownCount = this.shownItems.Count;
        LogInsertItemRequested(this.logger, parentLabel, itemLabel, relativeIndex, parentDepth, childrenCount, isExpanded, shownCount);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "RemoveItem requested: item='{item}' (IsRoot={isRoot}, IsLocked={isLocked}) (shownItems={shownCount})")]
    private static partial void LogRemoveItemRequested(ILogger logger, string item, bool isRoot, bool isLocked, int shownCount);

    [Conditional("DEBUG")]
    private void LogRemoveItemRequested(ITreeItem item)
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        var label = item.Label;
        var isRoot = item.IsRoot;
        var isLocked = item.IsLocked;
        var shownCount = this.shownItems.Count;
        LogRemoveItemRequested(this.logger, label, isRoot, isLocked, shownCount);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Completed removal of item '{item}' (removedShownItems={removedCount}, wasShown={wasShown})")]
    private static partial void LogRemoveItemCompleted(ILogger logger, string item, int removedCount, bool wasShown);

    [Conditional("DEBUG")]
    private void LogRemoveItemCompleted(ITreeItem item, int removedCount, bool wasShown)
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        var label = item.Label;
        LogRemoveItemCompleted(this.logger, label, removedCount, wasShown);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Relative index {originalIndex} for parent '{parent}' was out of range and adjusted to {clamped} (valid range [0, {childrenCount}])")]
    private static partial void LogRelativeIndexAdjusted(ILogger logger, string parent, int originalIndex, int clamped, int childrenCount);

    private void LogRelativeIndexAdjusted(ITreeItem parent, int originalIndex, int clamped)
    {
        if (!this.logger.IsEnabled(LogLevel.Warning))
        {
            return;
        }

        var label = parent.Label;
        var childrenCount = parent.ChildrenCount;
        LogRelativeIndexAdjusted(this.logger, label, originalIndex, clamped, childrenCount);
    }

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

    [Conditional("DEBUG")]
    private void LogFindNewItemIndexComputedNoSibling(ITreeItem parent, int relativeIndex, int index)
    {
        if (!this.logger.IsEnabled(LogLevel.Trace))
        {
            return;
        }

        var label = parent.Label;
        var depth = parent.Depth;
        var childrenCount = parent.ChildrenCount;
        LogFindNewItemIndexComputedNoSibling(this.logger, label, depth, relativeIndex, index, childrenCount);
    }

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
    private void LogFindNewItemIndexComputedWithSibling(ITreeItem parent, int relativeIndex, int index, string siblingRole, ITreeItem sibling)
    {
        if (!this.logger.IsEnabled(LogLevel.Trace))
        {
            return;
        }

        var label = parent.Label;
        var depth = parent.Depth;
        var childrenCount = parent.ChildrenCount;
        var siblingLabel = sibling.Label;
        var siblingDepth = sibling.Depth;
        LogFindNewItemIndexComputedWithSibling(this.logger, label, depth, relativeIndex, index, childrenCount, siblingRole, siblingLabel, siblingDepth);
    }

    [Conditional("DEBUG")]
    private void LogFindNewItemIndexComputed(ITreeItem parent, int relativeIndex, int index, string siblingRole, ITreeItem? sibling)
    {
        if (sibling is null)
        {
            this.LogFindNewItemIndexComputedNoSibling(parent, relativeIndex, index);
        }
        else
        {
            this.LogFindNewItemIndexComputedWithSibling(parent, relativeIndex, index, siblingRole, sibling);
        }
    }

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
        Message = "ShownItems.Insert: index={index}, item='{item}' (newCount={newCount})")]
    private static partial void LogShownItemsInsert(ILogger logger, int index, string item, int newCount);

    [Conditional("DEBUG")]
    private void LogShownItemsInsert(int index, ITreeItem item)
    {
        if (!this.logger.IsEnabled(LogLevel.Trace))
        {
            return;
        }

        var label = item.Label;
        var newCount = this.shownItems.Count + 1;
        LogShownItemsInsert(this.logger, index, label, newCount);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "ShownItems.RemoveAt: index={index}, item='{item}' (newCount={newCount})")]
    private static partial void LogShownItemsRemoveAt(ILogger logger, int index, string item, int newCount);

    [Conditional("DEBUG")]
    private void LogShownItemsRemoveAt(int index)
    {
        if (!this.logger.IsEnabled(LogLevel.Trace))
        {
            return;
        }

        var label = this.shownItems[index].Label;
        var newCount = this.shownItems.Count - 1;
        LogShownItemsRemoveAt(this.logger, index, label, newCount);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Item being added (approval decision) to parent '{parent}': item='{item}', proceed={proceed}")]
    private static partial void LogItemBeingAddedDecision(ILogger logger, string parent, string item, bool proceed);

    [Conditional("DEBUG")]
    private void LogItemBeingAddedDecision(ITreeItem parent, ITreeItem item, bool proceed)
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        var parentLabel = parent.Label;
        var itemLabel = item.Label;
        LogItemBeingAddedDecision(this.logger, parentLabel, itemLabel, proceed);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Item being removed (approval decision): item='{item}', proceed={proceed}")]
    private static partial void LogItemBeingRemovedDecision(ILogger logger, string item, bool proceed);

    [Conditional("DEBUG")]
    private void LogItemBeingRemovedDecision(ITreeItem item, bool proceed)
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        var label = item.Label;
        LogItemBeingRemovedDecision(this.logger, label, proceed);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "RemoveSingleSelectedItem: Processing selected item '{item}' (IsLocked={isLocked})")]
    private static partial void LogRemoveSingleSelectedItem(ILogger logger, string item, bool isLocked);

    [Conditional("DEBUG")]
    private void LogRemoveSingleSelectedItem(ITreeItem item)
    {
        if (!this.logger.IsEnabled(LogLevel.Trace))
        {
            return;
        }

        var label = item.Label;
        var isLocked = item.IsLocked;
        LogRemoveSingleSelectedItem(this.logger, label, isLocked);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "RemoveMultipleSelectionItems: Processing {selectedCount} items (originalShownItems={shownCount})")]
    private static partial void LogRemoveMultipleSelectionItemsStarted(ILogger logger, int selectedCount, int shownCount);

    [Conditional("DEBUG")]
    private void LogRemoveMultipleSelectionItemsStarted(int selectedCount)
    {
        if (!this.logger.IsEnabled(LogLevel.Trace))
        {
            return;
        }

        var shownCount = this.shownItems.Count;
        LogRemoveMultipleSelectionItemsStarted(this.logger, selectedCount, shownCount);
    }

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
    {
        if (!this.logger.IsEnabled(LogLevel.Trace))
        {
            return;
        }

        var label = parent.Label;
        var wasExpanded = parent.IsExpanded;
        LogParentAutoExpanded(this.logger, label, wasExpanded);
    }
}
