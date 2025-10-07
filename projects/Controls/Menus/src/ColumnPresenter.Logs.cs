// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Globalization;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;

namespace DroidNet.Controls.Menus;

#pragma warning disable SA1204 // Static elements should appear before instance elements

/// <summary>
///     Presents a single vertical column of <see cref="MenuItem"/> controls.
/// </summary>
public partial class ColumnPresenter
{
    [LoggerMessage(
        EventId = 3101,
        Level = LogLevel.Debug,
        Message = "[MenuColumnPresenter] ctor initialized")]
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
        EventId = 3102,
        Level = LogLevel.Error,
        Message = "[MenuColumnPresenter:{ColumnLevel}] (nav={NavMode}) FocusItem `{ItemId}` failed: no items source setup yet")]
    private static partial void LogFocusItemNoItemsSource(ILogger logger, int ColumnLevel, string itemId, MenuNavigationMode navMode);

    private void LogFocusItemNoItemsSource(MenuItemData item, MenuNavigationMode navMode)
    {
        if (this.MenuSource?.Services.FocusLogger is ILogger logger)
        {
            LogFocusItemNoItemsSource(logger, this.ColumnLevel, item.Id, navMode);
        }
    }

    [LoggerMessage(
        EventId = 3103,
        Level = LogLevel.Error,
        Message = "[MenuColumnPresenter:{ColumnLevel}] (nav={NavMode}) FocusItem `{ItemId}` failed: item not found")]
    private static partial void LogFocusItemNotFound(ILogger logger, int ColumnLevel, string itemId, MenuNavigationMode navMode);

    private void LogFocusItemNotFound(string itemId, MenuNavigationMode navMode)
    {
        if (this.MenuSource?.Services.FocusLogger is ILogger logger)
        {
            LogFocusItemNotFound(logger, this.ColumnLevel, itemId, navMode);
        }
    }

    private void LogFocusItemNotFound(int position, MenuNavigationMode navMode)
    {
        if (this.MenuSource?.Services.FocusLogger is ILogger logger)
        {
            LogFocusItemNotFound(logger, this.ColumnLevel, position.ToString(CultureInfo.InvariantCulture), navMode);
        }
    }

    [LoggerMessage(
        EventId = 3104,
        Level = LogLevel.Debug,
        Message = "[MenuColumnPresenter:{ColumnLevel}] (nav={NavMode}) FocusItemAt `{Position}` deferred")]
    private static partial void LogFocusDeferred(ILogger logger, int ColumnLevel, int position, MenuNavigationMode navMode);

    [Conditional("DEBUG")]
    private void LogFocusDeferred(int position, MenuNavigationMode navMode)
    {
        if (this.MenuSource?.Services.FocusLogger is ILogger logger)
        {
            LogFocusDeferred(logger, this.ColumnLevel, position, navMode);
        }
    }

    [LoggerMessage(
        EventId = 3105,
        Level = LogLevel.Warning,
        Message = "[MenuColumnPresenter:{ColumnLevel}] (nav={NavMode}) FocusItemAt `{Position}`, item `{ItemId}` is not focusable")]
    private static partial void LogItemNotFocusable(ILogger logger, int columnLevel, int position, MenuNavigationMode navMode, string ItemId);

    private void LogItemNotFocusable(string itemId, int position, MenuNavigationMode navMode)
    {
        if (this.MenuSource?.Services.FocusLogger is ILogger logger)
        {
            LogItemNotFocusable(logger, this.ColumnLevel, position, navMode, itemId);
        }
    }

    [LoggerMessage(
        EventId = 3106,
        Level = LogLevel.Debug,
        Message = "[MenuColumnPresenter:{ColumnLevel}] (nav={NavMode}) FocusItemAt {ItemId} at `{Position}`, item `{ItemId}` -> result={Result}")]
    private static partial void LogFocusItemResult(ILogger logger, int columnLevel, string itemId, int position, MenuNavigationMode navMode, bool result);

    [Conditional("DEBUG")]
    private void LogFocusItemResult(string itemId, int position, MenuNavigationMode navMode, bool result)
    {
        if (this.MenuSource?.Services.FocusLogger is ILogger logger)
        {
            LogFocusItemResult(logger, this.ColumnLevel, itemId, position, navMode, result);
        }
    }

    [LoggerMessage(
        EventId = 3110,
        Level = LogLevel.Debug,
        Message = "[MenuColumnPresenter:{ColumnLevel}] (nav={NavMode}) Processing deferred focus request for position {Position}")]
    private static partial void LogDeferredFocusProcess(ILogger logger, int columnLevel, int position, MenuNavigationMode navMode);

    [Conditional("DEBUG")]
    private void LogDeferredFocusProcess(int position, MenuNavigationMode navMode)
    {
        if (this.MenuSource?.Services.FocusLogger is ILogger logger)
        {
            LogDeferredFocusProcess(logger, this.ColumnLevel, position, navMode);
        }
    }

    [LoggerMessage(
        EventId = 3111,
        Level = LogLevel.Debug,
        Message = "[MenuColumnPresenter:{ColumnLevel}] populating with {Count} items...")]
    private static partial void LogPopulateItems(ILogger logger, int columnLevel, int count);

    [Conditional("DEBUG")]
    private void LogPopulateItems()
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            var count = this.ItemsSource?.OfType<MenuItemData>().Count() ?? 0;
            LogPopulateItems(logger, this.ColumnLevel, count);
        }
    }

    [LoggerMessage(
        EventId = 3112,
        Level = LogLevel.Debug,
        Message = "[MenuColumnPresenter:{ColumnLevel}] Clearing {Count} items...")]
    private static partial void LogClearItems(ILogger logger, int columnLevel, int count);

    [Conditional("DEBUG")]
    private void LogClearItems()
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            var count = this.itemsHost?.Children.OfType<MenuItem>().Count() ?? 0;
            LogClearItems(logger, this.ColumnLevel, count);
        }
    }

    [LoggerMessage(
        EventId = 3113,
        Level = LogLevel.Debug,
        Message = "[MenuColumnPresenter:{ColumnLevel}] Invoked {ItemId} source={InputSource}")]
    private static partial void LogItemInvoked(ILogger logger, int columnLevel, string itemId, MenuInteractionInputSource inputSource);

    [Conditional("DEBUG")]
    private void LogItemInvoked(string itemId, MenuInteractionInputSource inputSource)
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogItemInvoked(logger, this.ColumnLevel, itemId, inputSource);
        }
    }

    [LoggerMessage(
        EventId = 3114,
        Level = LogLevel.Debug,
        Message = "[MenuColumnPresenter:{ColumnLevel}] SubmenuRequested {ItemId} source={InputSource}")]
    private static partial void LogSubmenuRequested(ILogger logger, int columnLevel, string itemId, MenuInteractionInputSource inputSource);

    [Conditional("DEBUG")]
    private void LogSubmenuRequested(string itemId, MenuInteractionInputSource inputSource)
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogSubmenuRequested(logger, this.ColumnLevel, itemId, inputSource);
        }
    }

    [LoggerMessage(
        EventId = 3115,
        Level = LogLevel.Warning,
        Message = "[MenuColumnPresenter:{ColumnLevel}] SubmenuRequested ignored: controller changed before dispatch")]
    private static partial void LogSubmenuRequestIgnoredControllerChanged(ILogger logger, int columnLevel);

    private void LogSubmenuRequestIgnoredControllerChanged()
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogSubmenuRequestIgnoredControllerChanged(logger, this.ColumnLevel);
        }
    }

    [LoggerMessage(
        EventId = 3116,
        Level = LogLevel.Warning,
        Message = "[MenuColumnPresenter:{ColumnLevel}] GettingFocus aborted: invalid sender or item without data")]
    private static partial void LogGettingFocusAborted(ILogger logger, int columnLevel);

    private void LogGettingFocusAborted()
    {
        if (this.MenuSource?.Services.FocusLogger is ILogger logger)
        {
            LogGettingFocusAborted(logger, this.ColumnLevel);
        }
    }

    [LoggerMessage(
        EventId = 3117,
        Level = LogLevel.Debug,
        Message = "[MenuColumnPresenter:{ColumnLevel}] GotFocus {ItemId} state={FocusState}")]
    private static partial void LogGotFocus(ILogger logger, int columnLevel, string itemId, FocusState focusState);

    [Conditional("DEBUG")]
    private void LogGotFocus(string itemId, FocusState focusState)
    {
        if (this.MenuSource?.Services.FocusLogger is ILogger logger)
        {
            LogGotFocus(logger, this.ColumnLevel, itemId, focusState);
        }
    }

    [LoggerMessage(
        EventId = 3118,
        Level = LogLevel.Debug,
        Message = "[MenuColumnPresenter:{ColumnLevel}] HoverStarted {ItemId}")]
    private static partial void LogHoverStarted(ILogger logger, int columnLevel, string itemId);

    [Conditional("DEBUG")]
    private void LogHoverStarted(string itemId)
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogHoverStarted(logger, this.ColumnLevel, itemId);
        }
    }

    [LoggerMessage(
        EventId = 3119,
        Level = LogLevel.Debug,
        Message = "[MenuColumnPresenter:{ColumnLevel}] RadioGroupSelectionRequested {ItemId}")]
    private static partial void LogRadioGroupSelectionRequested(ILogger logger, int columnLevel, string itemId);

    [Conditional("DEBUG")]
    private void LogRadioGroupSelectionRequested(string itemId)
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogRadioGroupSelectionRequested(logger, this.ColumnLevel, itemId);
        }
    }

    [LoggerMessage(
        EventId = 3120,
        Level = LogLevel.Debug,
        Message = "[MenuColumnPresenter:{ColumnLevel}] GetAdjacentItem item={ItemId} direction={Direction} wrap={Wrap}")]
    private static partial void LogGetAdjacentItem(ILogger logger, int columnLevel, string itemId, MenuNavigationDirection direction, bool wrap);

    [Conditional("DEBUG")]
    private void LogGetAdjacentItem(string itemId, MenuNavigationDirection direction, bool wrap)
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogGetAdjacentItem(logger, this.ColumnLevel, itemId, direction, wrap);
        }
    }

    [LoggerMessage(
        EventId = 3121,
        Level = LogLevel.Debug,
        Message = "[MenuColumnPresenter:{ColumnLevel}] GetExpandedItem result={ItemId}")]
    private static partial void LogGetExpandedItem(ILogger logger, int columnLevel, string? itemId);

    [Conditional("DEBUG")]
    private void LogGetExpandedItem(string? itemId)
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogGetExpandedItem(logger, this.ColumnLevel, itemId);
        }
    }

    [LoggerMessage(
        EventId = 3122,
        Level = LogLevel.Debug,
        Message = "[MenuColumnPresenter:{ColumnLevel}] GetFocusedItem result={ItemId}")]
    private static partial void LogGetFocusedItem(ILogger logger, int columnLevel, string? itemId);

    [Conditional("DEBUG")]
    private void LogGetFocusedItem(string? itemId)
    {
        if (this.MenuSource?.Services.FocusLogger is ILogger logger)
        {
            LogGetFocusedItem(logger, this.ColumnLevel, itemId);
        }
    }
}
