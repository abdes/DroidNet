// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace DroidNet.Controls.Menus;

#pragma warning disable SA1204 // Static elements should appear before instance elements

/// <summary>
///     Presenter used by cascading menu hosts to render one or more menu levels.
/// </summary>
public partial class CascadedColumnsPresenter
{
    [LoggerMessage(
        EventId = 5001,
        Level = LogLevel.Debug,
        Message = "[CascadedColumnsPresenter] ctor initialized columns host")]
    private static partial void LogCreated(ILogger logger);

    [Conditional("DEBUG")]
    private void LogCreated()
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogCreated(logger);
        }
    }

    [LoggerMessage(
        EventId = 5002,
        Level = LogLevel.Debug,
        Message = "[CascadedColumnsPresenter] Reset, deferred focus: {HasDeferredFocus}, {ColumnsCount} columns and {HostChildrenCount} children cleared")]
    private static partial void LogReset(ILogger logger, bool hasDeferredFocus, int columnsCount, int hostChildrenCount);

    [Conditional("DEBUG")]
    private void LogReset()
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogReset(logger, this.deferredFocusRequest != null, this.columnPresenters.Count, this.columnsHost.Children.Count);
        }
    }

    [LoggerMessage(
        EventId = 5003,
        Level = LogLevel.Debug,
        Message = "[CascadedColumnsPresenter] Added column level {Level} with {ItemsCount} items (mode={Mode})")]
    private static partial void LogAddedColumn(ILogger logger, int Level, int ItemsCount, MenuNavigationMode Mode);

    [Conditional("DEBUG")]
    private void LogAddedColumn(int level, int itemsCount, MenuNavigationMode mode)
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogAddedColumn(logger, level, itemsCount, mode);
        }
    }

    [LoggerMessage(
        EventId = 5004,
        Level = LogLevel.Debug,
        Message = "[CascadedColumnsPresenter] Processing deferred focus request for column {ColumnLevel}")]
    private static partial void LogProcessingDeferredFocusRequest(ILogger logger, int ColumnLevel);

    [Conditional("DEBUG")]
    private void LogProcessingDeferredFocusRequest(int columnLevel)
    {
        if (this.MenuSource?.Services.FocusLogger is ILogger logger)
        {
            LogProcessingDeferredFocusRequest(logger, columnLevel);
        }
    }

    [LoggerMessage(
        EventId = 5005,
        Level = LogLevel.Debug,
        Message = "[CascadedColumnsPresenter] Processing deferred focus request for level={Level} position={Position} navMode={NavMode}")]
    private static partial void LogProcessingDeferredFocusRequestDetailed(ILogger logger, int level, int position, MenuNavigationMode navMode);

    [Conditional("DEBUG")]
    private void LogProcessingDeferredFocusRequestDetailed(int level, int position, MenuNavigationMode navMode)
    {
        if (this.MenuSource?.Services.FocusLogger is ILogger logger)
        {
            LogProcessingDeferredFocusRequestDetailed(logger, level, position, navMode);
        }
    }

    [LoggerMessage(
        EventId = 5006,
        Level = LogLevel.Debug,
        Message = "[CascadedColumnsPresenter] Deferred focus attempt result={Result} requestedPosition={Position}")]
    private static partial void LogDeferredFocusAttemptResult(ILogger logger, bool result, int Position);

    [Conditional("DEBUG")]
    private void LogDeferredFocusAttemptResult(bool result, int position)
    {
        if (this.MenuSource?.Services.FocusLogger is ILogger logger)
        {
            LogDeferredFocusAttemptResult(logger, result, position);
        }
    }

    [LoggerMessage(
        EventId = 5007,
        Level = LogLevel.Debug,
        Message = "[CascadedColumnsPresenter] No focusable child items for parent {ParentId} at column {ColumnLevel}")]
    private static partial void LogNoFocusableChildItems(ILogger logger, string ParentId, int ColumnLevel);

    [Conditional("DEBUG")]
    private void LogNoFocusableChildItems(string parentId, int columnLevel)
    {
        if (this.MenuSource?.Services.FocusLogger is ILogger logger)
        {
            LogNoFocusableChildItems(logger, parentId, columnLevel);
        }
    }

    [LoggerMessage(
        EventId = 5008,
        Level = LogLevel.Debug,
        Message = "[CascadedColumnsPresenter] Scheduling focus for child item {ItemId} at column {ColumnLevel} (source={Source})")]
    private static partial void LogSchedulingFocusForChildItem(ILogger logger, string ItemId, int ColumnLevel, MenuInteractionInputSource Source);

    [Conditional("DEBUG")]
    private void LogSchedulingFocusForChildItem(string itemId, int columnLevel, MenuInteractionInputSource source)
    {
        if (this.MenuSource?.Services.FocusLogger is ILogger logger)
        {
            LogSchedulingFocusForChildItem(logger, itemId, columnLevel, source);
        }
    }

    [LoggerMessage(
        EventId = 5009,
        Level = LogLevel.Debug,
        Message = "[CascadedColumnsPresenter] Controller lost before child focus dispatch")]
    private static partial void LogControllerLostBeforeChildFocusDispatch(ILogger logger);

    [Conditional("DEBUG")]
    private void LogControllerLostBeforeChildFocusDispatch()
    {
        if (this.MenuSource?.Services.FocusLogger is ILogger logger)
        {
            LogControllerLostBeforeChildFocusDispatch(logger);
        }
    }

    [LoggerMessage(
        EventId = 5010,
        Level = LogLevel.Debug,
        Message = "[CascadedColumnsPresenter] Child focus dispatch skipped: controller changed before focus for {ItemId}")]
    private static partial void LogChildFocusDispatchSkippedControllerChanged(ILogger logger, string ItemId);

    [Conditional("DEBUG")]
    private void LogChildFocusDispatchSkippedControllerChanged(string itemId)
    {
        if (this.MenuSource?.Services.FocusLogger is ILogger logger)
        {
            LogChildFocusDispatchSkippedControllerChanged(logger, itemId);
        }
    }

    [LoggerMessage(
        EventId = 5011,
        Level = LogLevel.Debug,
        Message = "[CascadedColumnsPresenter] Trim columns to level {ColumnLevel}, remaining {RemainingCount} out of {InitialCount}")]
    private static partial void LogTrimColumns(ILogger logger, int columnLevel, int initialCount, int remainingCount);

    [Conditional("DEBUG")]
    private void LogTrimColumns(int columnLevel, int initialCount)
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogTrimColumns(logger, columnLevel, initialCount, this.columnPresenters.Count);
        }
    }

    [LoggerMessage(
        EventId = 5012,
        Level = LogLevel.Debug,
        Message = "[CascadedColumnsPresenter] MenuSource changed (current columns = {ColumnCount})")]
    private static partial void LogMenuSourceChanged(ILogger logger, int columnCount);

    [Conditional("DEBUG")]
    private void LogMenuSourceChanged()
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogMenuSourceChanged(logger, this.columnPresenters.Count);
        }
    }

    [LoggerMessage(
        EventId = 5013,
        Level = LogLevel.Debug,
        Message = "[CascadedColumnsPresenter] CloseFromColumn {Level} (columns={ColumnCount}")]
    private static partial void LogCloseFromColumn(ILogger logger, int level, int columnCount);

    [Conditional("DEBUG")]
    private void LogCloseFromColumn(int level)
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogCloseFromColumn(logger, level, this.columnPresenters.Count);
        }
    }

    [LoggerMessage(
        EventId = 5014,
        Level = LogLevel.Error,
        Message = "[CascadedColumnsPresenter] CloseFromColumn ignored: invalid level {Level} (columns={ColumnCount})")]
    private static partial void LogCloseFromColumnIgnored(ILogger logger, int level, int columnCount);

    private void LogCloseFromColumnIgnored(int level)
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogCloseFromColumnIgnored(logger, level, this.columnPresenters.Count);
        }
    }

    [LoggerMessage(
        EventId = 5015,
        Level = LogLevel.Debug,
        Message = "[CascadedColumnsPresenter] FocusColumnItem level={Level}, item={ItemId}, mode={NavMode}")]
    private static partial void LogFocusItem(ILogger logger, int level, string itemId, MenuNavigationMode navMode);

    [Conditional("DEBUG")]
    private void LogFocusItem(int level, string itemId, MenuNavigationMode mode)
    {
        if (this.MenuSource?.Services.FocusLogger is ILogger logger)
        {
            LogFocusItem(logger, level, itemId, mode);
        }
    }

    [LoggerMessage(
        EventId = 5016,
        Level = LogLevel.Debug,
        Message = "[CascadedColumnsPresenter] FocusFirstItem level={Level}, mode={NavMode}")]
    private static partial void LogFocusFirstItem(ILogger logger, int level, MenuNavigationMode navMode);

    [Conditional("DEBUG")]
    private void LogFocusFirstItem(int level, MenuNavigationMode mode)
    {
        if (this.MenuSource?.Services.FocusLogger is ILogger logger)
        {
            LogFocusFirstItem(logger, level, mode);
        }
    }

    [LoggerMessage(
        EventId = 5017,
        Level = LogLevel.Debug,
        Message = "[CascadedColumnsPresenter] Deferred FocusColumnFirstItem level={Level}, mode={NavMode}")]
    private static partial void LogDeferredFocusColumnFirstItem(ILogger logger, int level, MenuNavigationMode navMode);

    [Conditional("DEBUG")]
    private void LogDeferredFocusColumnFirstItem(int level, MenuNavigationMode mode)
    {
        if (this.MenuSource?.Services.FocusLogger is ILogger logger)
        {
            LogDeferredFocusColumnFirstItem(logger, level, mode);
        }
    }

    [LoggerMessage(
        EventId = 5018,
        Level = LogLevel.Error,
        Message = "[CascadedColumnsPresenter] OpenChildColumn aborted: menu source is {MenuSourceState}, controller is {ControllerState}")]
    private static partial void LogOpenChildColumnAborted(ILogger logger, string menuSourceState, string controllerState);

    private void LogOpenChildColumnAborted()
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            var menuSourceNull = this.MenuSource == null ? "null" : "valid";
            var controllerNull = this.MenuSource?.Services.InteractionController == null ? "null" : "valid";
            LogOpenChildColumnAborted(logger, menuSourceNull, controllerNull);
        }
    }

    [LoggerMessage(
        EventId = 5019,
        Level = LogLevel.Warning,
        Message = "[CascadedColumnsPresenter] Parent {ParentId} has no children, skipping column open")]
    private static partial void LogParentHasNoChildren(ILogger logger, string ParentId);

    [Conditional("DEBUG")]
    private void LogParentHasNoChildren(string parentId)
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogParentHasNoChildren(logger, parentId);
        }
    }

    [LoggerMessage(
        EventId = 5020,
        Level = LogLevel.Debug,
        Message = "[CascadedColumnsPresenter] Opening child column level {Level} for parent {ParentId} with {ItemsCount} items (mode={Mode})")]
    private static partial void LogOpeningChildColumn(ILogger logger, int Level, string ParentId, int ItemsCount, MenuNavigationMode Mode);

    [Conditional("DEBUG")]
    private void LogOpeningChildColumn(int level, string parentId, int itemsCount, MenuNavigationMode mode)
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogOpeningChildColumn(logger, level, parentId, itemsCount, mode);
        }
    }

    [LoggerMessage(
        EventId = 5021,
        Level = LogLevel.Debug,
        Message = "[CascadedColumnsPresenter] Dismiss requested (kind={Kind})")]
    private static partial void LogDismiss(ILogger logger, MenuDismissKind kind);

    [Conditional("DEBUG")]
    private void LogDismiss(MenuDismissKind kind)
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogDismiss(logger, kind);
        }
    }

    [LoggerMessage(
        EventId = 5022,
        Level = LogLevel.Debug,
        Message = "[CascadedColumnsPresenter] GetAdjacentItem level={Level} item={ItemId} direction={Direction} wrap={Wrap}")]
    private static partial void LogGetAdjacentItem(ILogger logger, int level, string itemId, MenuNavigationDirection direction, bool wrap);

    [Conditional("DEBUG")]
    private void LogGetAdjacentItem(int level, string itemId, MenuNavigationDirection direction, bool wrap)
    {
        if (this.MenuSource?.Services.FocusLogger is ILogger logger)
        {
            LogGetAdjacentItem(logger, level, itemId, direction, wrap);
        }
    }

    [LoggerMessage(
        EventId = 5023,
        Level = LogLevel.Debug,
        Message = "[CascadedColumnsPresenter] GetExpandedItem level={Level} result={ItemId}")]
    private static partial void LogGetExpandedItem(ILogger logger, int level, string? itemId);

    [Conditional("DEBUG")]
    private void LogGetExpandedItem(int level, string? itemId)
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogGetExpandedItem(logger, level, itemId);
        }
    }

    [LoggerMessage(
        EventId = 5024,
        Level = LogLevel.Debug,
        Message = "[CascadedColumnsPresenter] GetFocusedItem level={Level} result={ItemId}")]
    private static partial void LogGetFocusedItem(ILogger logger, int level, string? itemId);

    [Conditional("DEBUG")]
    private void LogGetFocusedItem(int level, string? itemId)
    {
        if (this.MenuSource?.Services.FocusLogger is ILogger logger)
        {
            LogGetFocusedItem(logger, level, itemId);
        }
    }

    [LoggerMessage(
        EventId = 5025,
        Level = LogLevel.Debug,
        Message = "[CascadedColumnsPresenter] ExpandItem level={Level} item={ItemId} mode={Mode}")]
    private static partial void LogExpandItem(ILogger logger, int level, string itemId, MenuNavigationMode mode);

    [Conditional("DEBUG")]
    private void LogExpandItem(int level, string itemId, MenuNavigationMode mode)
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogExpandItem(logger, level, itemId, mode);
        }
    }

    [LoggerMessage(
        EventId = 5026,
        Level = LogLevel.Debug,
        Message = "[CascadedColumnsPresenter] CollapseItem level={Level} item={ItemId} mode={Mode}")]
    private static partial void LogCollapseItem(ILogger logger, int level, string itemId, MenuNavigationMode mode);

    [Conditional("DEBUG")]
    private void LogCollapseItem(int level, string itemId, MenuNavigationMode mode)
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogCollapseItem(logger, level, itemId, mode);
        }
    }

    [LoggerMessage(
        EventId = 5027,
        Level = LogLevel.Error,
        Message = "[CascadedColumnsPresenter] AddColumn failed: controller not set")]
    private static partial void LogAddColumnControllerNotSet(ILogger logger);

    private void LogAddColumnControllerNotSet()
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogAddColumnControllerNotSet(logger);
        }
    }

    [LoggerMessage(
        EventId = 5028,
        Level = LogLevel.Error,
        Message = "[CascadedColumnsPresenter] Invalid column level {Level} requested (total columns={ColumnCount})")]
    private static partial void LogInvalidColumnLevel(ILogger logger, int level, int columnCount);

    private void LogInvalidColumnLevel(int level)
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogInvalidColumnLevel(logger, level, this.columnPresenters.Count);
        }
    }
}
