// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
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

    [LoggerMessage(
        Level = LogLevel.Trace,
        Message = "FilteredItems changed: action={action}, newIndex={newStartingIndex}, oldIndex={oldStartingIndex}, newCount={newCount}, oldCount={oldCount}, newItems={newItems}, oldItems={oldItems}")]
    private static partial void LogFilteredItemsChanged(
        ILogger logger,
        NotifyCollectionChangedAction action,
        int newStartingIndex,
        int oldStartingIndex,
        int newCount,
        int oldCount,
        string? newItems,
        string? oldItems);

    [Conditional("DEBUG")]
    private void LogFilteredItemsChanged(
        NotifyCollectionChangedEventArgs e,
        string? newItems,
        string? oldItems)
        => LogFilteredItemsChanged(
            this.logger,
            e.Action,
            e.NewStartingIndex,
            e.OldStartingIndex,
            e.NewItems?.Count ?? 0,
            e.OldItems?.Count ?? 0,
            newItems,
            oldItems);
}
