// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections;
using System.Collections.Specialized;
using System.Diagnostics;
using System.Text;
using Microsoft.Extensions.Logging;

namespace DroidNet.Controls;

/// <summary>
///     Logging helpers for <see cref="DynamicTreeViewModel"/>.
/// </summary>
public partial class DynamicTreeViewModel
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Filtering cache invalidated: rev={Revision} reason={Reason}")]
    private static partial void LogFilteringCacheInvalidated(ILogger logger, long revision, string reason);

    [Conditional("DEBUG")]
    private void LogFilteringCacheInvalidated(long revision, string reason)
        => LogFilteringCacheInvalidated(this.logger, revision, reason);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Filtering cache recomputed: rev={Revision} reason={Reason} visitedNodes={VisitedNodes} visitedEdges={VisitedEdges} elapsedMs={ElapsedMs}")]
    private static partial void LogFilteringCacheRecomputed(
        ILogger logger,
        long revision,
        string reason,
        int visitedNodes,
        int visitedEdges,
        long elapsedMs);

    [Conditional("DEBUG")]
    private void LogFilteringCacheRecomputed(long revision, string reason, int visitedNodes, int visitedEdges, long elapsedMs)
        => LogFilteringCacheRecomputed(this.logger, revision, reason, visitedNodes, visitedEdges, elapsedMs);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Cannot expand item '{item}': item is not root, and its parent '{parent}' is not expanded")]
    private static partial void LogExpandItemNotVisible(ILogger logger, string item, string parent);

    private void LogExpandItemNotVisible(ITreeItem item)
    {
        if (!this.logger.IsEnabled(LogLevel.Error))
        {
            return;
        }

        var label = item.Label;
        var parent = item.Parent?.Label ?? "<null>";
        LogExpandItemNotVisible(this.logger, label, parent);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Expanding item '{item}': Depth={depth}, Children={childrenCount}")]
    private static partial void LogExpandItem(ILogger logger, string item, int depth, int childrenCount);

    [Conditional("DEBUG")]
    private void LogExpandItem(ITreeItem item)
    {
        if (!this.logger.IsEnabled(LogLevel.Trace))
        {
            return;
        }

        var label = item.Label;
        var depth = item.Depth;
        var childrenCount = item.ChildrenCount;
        LogExpandItem(this.logger, label, depth, childrenCount);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Initializing root '{root}' (skipRoot={skipRoot}, IsExpanded={isExpanded})")]
    private static partial void LogInitializeRoot(ILogger logger, string root, bool skipRoot, bool isExpanded);

    [Conditional("DEBUG")]
    private void LogInitializeRoot(ITreeItem root, bool skipRoot)
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        var label = root.Label;
        var isExpanded = root.IsExpanded;
        LogInitializeRoot(this.logger, label, skipRoot, isExpanded);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "FocusItem called with item='{Item}' origin={Origin}")]
    private static partial void LogFocusItemCalled(ILogger logger, string? item, RequestOrigin origin);

    [Conditional("DEBUG")]
    private void LogFocusItemCalled(ITreeItem? item, RequestOrigin origin)
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        var label = item?.Label;
        LogFocusItemCalled(this.logger, label, origin);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "RestoreExpandedChildren: item='{item}' (childrenCount={childrenCount})")]
    private static partial void LogRestoreExpandedChildrenStarted(ILogger logger, string item, int childrenCount);

    [Conditional("DEBUG")]
    private void LogRestoreExpandedChildrenStarted(ITreeItem item)
    {
        if (!this.logger.IsEnabled(LogLevel.Trace))
        {
            return;
        }

        var label = item.Label;
        var childrenCount = item.ChildrenCount;
        LogRestoreExpandedChildrenStarted(this.logger, label, childrenCount);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "HideChildren: item='{item}' (childrenCount={childrenCount})")]
    private static partial void LogHideChildrenStarted(ILogger logger, string item, int childrenCount);

    [Conditional("DEBUG")]
    private void LogHideChildrenStarted(ITreeItem item)
    {
        if (!this.logger.IsEnabled(LogLevel.Trace))
        {
            return;
        }

        var label = item.Label;
        var childrenCount = item.ChildrenCount;
        LogHideChildrenStarted(this.logger, label, childrenCount);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "HideChildren: item='{item}' is not in the shown list; its subtree is already hidden.")]
    private static partial void LogHideChildrenNotShown(ILogger logger, string item);

    [Conditional("DEBUG")]
    private void LogHideChildrenNotShown(ITreeItem item)
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        var label = item.Label;
        LogHideChildrenNotShown(this.logger, label);
    }

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
    {
        if (!this.logger.IsEnabled(LogLevel.Warning))
        {
            return;
        }

        var type = item.GetType();
        var itemType = type.FullName ?? type.Name;
        LogCopySkippedNonClonableItem(this.logger, itemType, nameof(ICanBeCloned));
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Paste skipped: target '{target}' is the source cut item '{source}'")]
    private static partial void LogPasteSkippedIntoSource(ILogger logger, string target, string source);

    private void LogPasteSkippedIntoSource(ITreeItem item)
    {
        if (!this.logger.IsEnabled(LogLevel.Warning))
        {
            return;
        }

        var label = item.Label;
        LogPasteSkippedIntoSource(this.logger, label, label);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Paste skipped: target '{target}' is a descendant of cut item '{source}'")]
    private static partial void LogPasteSkippedIntoDescendant(ILogger logger, string target, string source);

    private void LogPasteSkippedIntoDescendant(ITreeItem source, ITreeItem target)
    {
        if (!this.logger.IsEnabled(LogLevel.Warning))
        {
            return;
        }

        var targetLabel = target.Label;
        var sourceLabel = source.Label;
        LogPasteSkippedIntoDescendant(this.logger, targetLabel, sourceLabel);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "ShownItems.Add: item='{item}' (newCount={newCount})")]
    private static partial void LogShownItemsAdd(ILogger logger, string item, int newCount);

    [Conditional("DEBUG")]
    private void LogShownItemsAdd(ITreeItem item)
    {
        if (!this.logger.IsEnabled(LogLevel.Trace))
        {
            return;
        }

        var label = item.Label;
        var newCount = this.shownItems.Count + 1;
        LogShownItemsAdd(this.logger, label, newCount);
    }

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
        Level = LogLevel.Trace,
        Message = "ShownItems.Clear (shownItemsCount={count})")]
    private static partial void LogShownItemsClear(ILogger logger, int count);

    [Conditional("DEBUG")]
    private void LogShownItemsClear()
    {
        if (!this.logger.IsEnabled(LogLevel.Trace))
        {
            return;
        }

        var count = this.shownItems.Count;
        LogShownItemsClear(this.logger, count);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Did you forget to focus item '{Item}' before selecting it? (RequestOrigin={Origin})")]
    private static partial void LogForgotToFocusItem(ILogger logger, string item, RequestOrigin origin);

    private void LogForgotToFocusItem(ITreeItem item, RequestOrigin origin)
    {
        if (!this.logger.IsEnabled(LogLevel.Warning))
        {
            return;
        }

        var label = item.Label;
        LogForgotToFocusItem(this.logger, label, origin);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
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

    /// <summary>Describes up to six items of a changed range, followed by the count of the rest.</summary>
    /// <param name="items">The changed items, if any.</param>
    /// <returns>The description, or <see langword="null"/> when there are no items.</returns>
    private static string? DescribeItems(IList? items)
    {
        if (items is null || items.Count == 0)
        {
            return null;
        }

        const int maxItemsToLog = 6;
        var max = items.Count < maxItemsToLog ? items.Count : maxItemsToLog;

        var sb = new StringBuilder();
        _ = sb.Append('[');
        for (var i = 0; i < max; i++)
        {
            if (i > 0)
            {
                _ = sb.Append(", ");
            }

            _ = items[i] is ITreeItem item
                ? sb.Append(item.Label)
                : sb.Append(items[i]?.ToString() ?? "<null>");
        }

        if (items.Count > max)
        {
            _ = sb.Append(", …+").Append(items.Count - max);
        }

        return sb.Append(']').ToString();
    }

    [Conditional("DEBUG")]
    private void LogFilteredItemsChanged(NotifyCollectionChangedEventArgs e)
    {
        if (!this.logger.IsEnabled(LogLevel.Trace))
        {
            return;
        }

        var newCount = e.NewItems?.Count ?? 0;
        var oldCount = e.OldItems?.Count ?? 0;
        var newItems = DescribeItems(e.NewItems);
        var oldItems = DescribeItems(e.OldItems);
        LogFilteredItemsChanged(this.logger, e.Action, e.NewStartingIndex, e.OldStartingIndex, newCount, oldCount, newItems, oldItems);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "{ViewModel} is already shown by a loaded tree; a second tree over it is rejected and shows nothing, because a view model has one live tree at a time")]
    private static partial void LogSecondTreeRejected(ILogger logger, string viewModel);

    private void LogSecondTreeRejected()
    {
        if (!this.logger.IsEnabled(LogLevel.Error))
        {
            return;
        }

        var viewModel = this.GetType().Name;
        LogSecondTreeRejected(this.logger, viewModel);
    }
}
