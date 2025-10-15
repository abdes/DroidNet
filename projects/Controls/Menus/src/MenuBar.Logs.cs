// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace DroidNet.Controls.Menus;

#pragma warning disable SA1204 // Static elements should appear before instance elements

/// <summary>
///     Horizontal menu bar control that renders root <see cref="MenuItemData"/> instances and
///     materializes cascading submenus through an <see cref="ICascadedMenuHost"/>.
/// </summary>
public partial class MenuBar
{
    [LoggerMessage(
        EventId = 3201,
        Level = LogLevel.Debug,
        Message = "[MenuBar] Dismiss() called")]
    private static partial void LogDismiss(ILogger logger);

    [Conditional("DEBUG")]
    private void LogDismiss()
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogDismiss(logger);
        }
    }

    [LoggerMessage(
        EventId = 3202,
        Level = LogLevel.Debug,
        Message = "[MenuBar] Dismissing host programmatically")]
    private static partial void LogDismissingHost(ILogger logger);

    [Conditional("DEBUG")]
    private void LogDismissingHost()
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogDismissingHost(logger);
        }
    }

    [LoggerMessage(
        EventId = 3203,
        Level = LogLevel.Debug,
        Message = "[MenuBar] FocusItem {ItemId}, mode={NavMode}, result={Result}")]
    private static partial void LogFocusItem(ILogger logger, string itemId, MenuNavigationMode navMode, bool result);

    [Conditional("DEBUG")]
    private void LogFocusItem(string itemId, MenuNavigationMode navMode, bool result)
    {
        if (this.MenuSource?.Services.FocusLogger is ILogger logger)
        {
            LogFocusItem(logger, itemId, navMode, result);
        }
    }

    [LoggerMessage(
        EventId = 3204,
        Level = LogLevel.Debug,
        Message = "[MenuBar] ExpandItem {ItemId}, mode={NavMode}")]
    private static partial void LogExpandItem(ILogger logger, string itemId, MenuNavigationMode navMode);

    [Conditional("DEBUG")]
    private void LogExpandItem(string itemId, MenuNavigationMode navMode)
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogExpandItem(logger, itemId, navMode);
        }
    }

    [LoggerMessage(
        EventId = 3205,
        Level = LogLevel.Debug,
        Message = "[MenuBar] Setting MenuSource on host")]
    private static partial void LogSettingMenuSourceOnHost(ILogger logger);

    [Conditional("DEBUG")]
    private void LogSettingMenuSourceOnHost()
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogSettingMenuSourceOnHost(logger);
        }
    }

    [LoggerMessage(
        EventId = 3206,
        Level = LogLevel.Debug,
        Message = "[MenuBar] Showing host")]
    private static partial void LogShowingHost(ILogger logger);

    [Conditional("DEBUG")]
    private void LogShowingHost()
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogShowingHost(logger);
        }
    }

    [LoggerMessage(
        EventId = 3207,
        Level = LogLevel.Debug,
        Message = "[MenuBar] CollapseItem {ItemId}, mode={NavMode}")]
    private static partial void LogCollapseItem(ILogger logger, string itemId, MenuNavigationMode navMode);

    [Conditional("DEBUG")]
    private void LogCollapseItem(string itemId, MenuNavigationMode navMode)
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogCollapseItem(logger, itemId, navMode);
        }
    }

    [LoggerMessage(
        EventId = 3208,
        Level = LogLevel.Warning,
        Message = "[MenuBar] Event aborted: invalid sender or item without data")]
    private static partial void LogEventAborted(ILogger logger);

    private void LogEventAborted()
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogEventAborted(logger);
        }
    }

    [LoggerMessage(
        EventId = 3209,
        Level = LogLevel.Debug,
        Message = "[MenuBar] OnHostClosed: Active cascaded menu host closed")]
    private static partial void LogHostClosed(ILogger logger);

    [Conditional("DEBUG")]
    private void LogHostClosed()
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogHostClosed(logger);
        }
    }

    [LoggerMessage(
        EventId = 3210,
        Level = LogLevel.Debug,
        Message = "[MenuBar] OnHostOpening: set IsExpanded for {ItemId}")]
    private static partial void LogHostOpening(ILogger logger, string itemId);

    [Conditional("DEBUG")]
    private void LogHostOpening(string itemId)
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogHostOpening(logger, itemId);
        }
    }

    [LoggerMessage(
        EventId = 3211,
        Level = LogLevel.Debug,
        Message = "[MenuBar] ItemInvoked {ItemId}, inputSource={InputSource}")]
    private static partial void LogItemInvoked(ILogger logger, string itemId, MenuInteractionInputSource inputSource);

    [Conditional("DEBUG")]
    private void LogItemInvoked(string itemId, MenuInteractionInputSource inputSource)
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogItemInvoked(logger, itemId, inputSource);
        }
    }

    [LoggerMessage(
        EventId = 3212,
        Level = LogLevel.Debug,
        Message = "[MenuBar] HoverStarted {ItemId}, {WillExpand}")]
    private static partial void LogHoverStarted(ILogger logger, string itemId, string willExpand);

    [Conditional("DEBUG")]
    private void LogHoverStarted(string itemId, bool willExpand)
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogHoverStarted(logger, itemId, willExpand ? "will expand" : "will not expand");
        }
    }

    [LoggerMessage(
        EventId = 3213,
        Level = LogLevel.Debug,
        Message = "[MenuBar] HoverEnded {ItemId}")]
    private static partial void LogHoverEnded(ILogger logger, string itemId);

    [Conditional("DEBUG")]
    private void LogHoverEnded(string itemId)
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogHoverEnded(logger, itemId);
        }
    }

    [LoggerMessage(
        EventId = 3214,
        Level = LogLevel.Debug,
        Message = "[MenuBar] GotFocus {ItemId}, inputSource={InputSource}")]
    private static partial void LogGotFocus(ILogger logger, string itemId, MenuInteractionInputSource inputSource);

    [Conditional("DEBUG")]
    private void LogGotFocus(string itemId, MenuInteractionInputSource inputSource)
    {
        if (this.MenuSource?.Services.FocusLogger is ILogger logger)
        {
            LogGotFocus(logger, itemId, inputSource);
        }
    }

    [LoggerMessage(
        EventId = 3215,
        Level = LogLevel.Debug,
        Message = "[MenuBar] LostFocus {ItemId}")]
    private static partial void LogLostFocus(ILogger logger, string itemId);

    [Conditional("DEBUG")]
    private void LogLostFocus(string itemId)
    {
        if (this.MenuSource?.Services.FocusLogger is ILogger logger)
        {
            LogLostFocus(logger, itemId);
        }
    }

    [LoggerMessage(
        EventId = 3216,
        Level = LogLevel.Debug,
        Message = "[MenuBar] SubmenuRequested {ItemId}, inputSource={InputSource}, result={Result}")]
    private static partial void LogSubmenuRequested(ILogger logger, string itemId, MenuInteractionInputSource inputSource, bool result);

    [Conditional("DEBUG")]
    private void LogSubmenuRequested(string itemId, MenuInteractionInputSource inputSource, bool result)
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogSubmenuRequested(logger, itemId, inputSource, result);
        }
    }

    [LoggerMessage(
        EventId = 3217,
        Level = LogLevel.Debug,
        Message = "[MenuBar] GetExpandedItem result={ItemId}")]
    private static partial void LogGetExpandedItem(ILogger logger, string? itemId);

    [Conditional("DEBUG")]
    private void LogGetExpandedItem(string? itemId)
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogGetExpandedItem(logger, itemId);
        }
    }

    [LoggerMessage(
        EventId = 3218,
        Level = LogLevel.Debug,
        Message = "[MenuBar] GetFocusedItem result={ItemId}")]
    private static partial void LogGetFocusedItem(ILogger logger, string? itemId);

    [Conditional("DEBUG")]
    private void LogGetFocusedItem(string? itemId)
    {
        if (this.MenuSource?.Services.FocusLogger is ILogger logger)
        {
            LogGetFocusedItem(logger, itemId);
        }
    }

    [LoggerMessage(
        EventId = 3219,
        Level = LogLevel.Debug,
        Message = "[MenuBar] GetAdjacentItem item={ItemId} direction={Direction} wrap={Wrap}")]
    private static partial void LogGetAdjacentItem(ILogger logger, string itemId, MenuNavigationDirection direction, bool wrap);

    [Conditional("DEBUG")]
    private void LogGetAdjacentItem(string itemId, MenuNavigationDirection direction, bool wrap)
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogGetAdjacentItem(logger, itemId, direction, wrap);
        }
    }

    [LoggerMessage(
        EventId = 3220,
        Level = LogLevel.Debug,
        Message = "[MenuBar] Cascaded menu host opened (item={ItemId})")]
    private static partial void LogHostOpened(ILogger logger, string itemId);

    [Conditional("DEBUG")]
    private void LogHostOpened(MenuItemData expanded)
    {
        if (this.MenuSource?.Services.InteractionLogger is ILogger logger)
        {
            LogHostOpened(logger, expanded.Id);
        }
    }

    [LoggerMessage(
        EventId = 3221,
        Level = LogLevel.Debug,
        Message = "[MenuBar] Pointer Captured (Captor=`{ItemId}`, Pointer=`{PointerId}`")]
    private static partial void LogCaptureSuccess(ILogger logger, string itemId, uint pointerId);

    [Conditional("DEBUG")]
    private void LogCaptureSuccess()
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger && this.capture is not null)
        {
            var cv = this.capture.Value;
            if (cv.TryGetCaptor(out var item) && item is MenuItem { ItemData: { } itemData })
            {
                LogCaptureSuccess(logger, itemData.Id, cv.PointerId);
            }
        }
    }

    [LoggerMessage(
        EventId = 3222,
        Level = LogLevel.Warning,
        Message = "[MenuBar] Pointer Capture failed")]
    private static partial void LogCaptureFailed(ILogger logger);

    private void LogCaptureFailed()
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogCaptureFailed(logger);
        }
    }
}
