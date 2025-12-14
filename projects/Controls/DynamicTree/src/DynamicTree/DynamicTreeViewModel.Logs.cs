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
        Level = LogLevel.Debug,
        Message = "FocusItem called with item='{Item}' origin={Origin}")]
    private static partial void LogFocusItemCalled(ILogger logger, string? item, RequestOrigin origin);

    private void LogFocusItemCalled(ITreeItem? item, RequestOrigin origin)
        => LogFocusItemCalled(this.logger, item?.Label, origin);

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
        => LogShownItemsAdd(this.logger, item.Label, this.shownItems.Count + 1);

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
        Level = LogLevel.Trace,
        Message = "ShownItems.Clear (shownItemsCount={count})")]
    private static partial void LogShownItemsClear(ILogger logger, int count);

    [Conditional("DEBUG")]
    private void LogShownItemsClear()
        => LogShownItemsClear(this.logger, this.shownItems.Count);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Did you forget to focus item '{Item}' before selecting it? (RequestOrigin={Origin})")]
    private static partial void LogForgotToFocusItem(ILogger logger, string item, RequestOrigin origin);

    [Conditional("DEBUG")]
    private void LogForgotToFocusItem(ITreeItem item, RequestOrigin origin)
        => LogForgotToFocusItem(this.logger, item.Label, origin);
}
