// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

#pragma warning disable SA1204 // Static elements should appear before instance elements

using System.Diagnostics;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls.Primitives;
using Windows.Foundation;

namespace DroidNet.Controls.Menus;

/// <summary>
///     <see cref="ICascadedMenuHost"/> implementation backed by <see cref="Popup"/>.
///     Acts as its own <see cref="ICascadedMenuSurface"/>.
/// </summary>
internal sealed partial class PopupMenuHost
{
    private const string UnknownAnchorId = "UNKNOWN";

    private static string GetAnchorId(FrameworkElement? anchor) => anchor switch
    {
        MenuItem { ItemData: { } data } => data.Id,
        FrameworkElement fe when !string.IsNullOrEmpty(fe.Name) => fe.Name,
        _ => UnknownAnchorId,
    };

    [LoggerMessage(
        EventId = 3201,
        Level = LogLevel.Debug,
        Message = "[PopupMenuHost] ShowAt anchor={AnchorId}, mode={NavigationMode}, bounds={AnchorBounds}")]
    private static partial void LogShowAt(ILogger logger, string AnchorId, MenuNavigationMode NavigationMode, Rect AnchorBounds);

    [Conditional("DEBUG")]
    private void LogShowAt(MenuItem anchorItem, MenuNavigationMode navigationMode, Rect anchorBounds)
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            var anchorId = GetAnchorId(anchorItem);
            LogShowAt(logger, anchorId, navigationMode, anchorBounds);
        }
    }

    [LoggerMessage(
        EventId = 3202,
        Level = LogLevel.Debug,
        Message = "[PopupMenuHost] Dismiss requested ({Kind})")]
    private static partial void LogDismissRequested(ILogger logger, MenuDismissKind Kind);

    [Conditional("DEBUG")]
    private void LogDismissRequested(MenuDismissKind kind)
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogDismissRequested(logger, kind);
        }
    }

    [LoggerMessage(
        EventId = 3203,
        Level = LogLevel.Debug,
        Message = "[PopupMenuHost] Dismiss ignored ({Kind}) because popup is not open")]
    private static partial void LogDismissIgnored(ILogger logger, MenuDismissKind Kind);

    [Conditional("DEBUG")]
    private void LogDismissIgnored(MenuDismissKind kind)
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogDismissIgnored(logger, kind);
        }
    }

    [LoggerMessage(
        EventId = 3204,
        Level = LogLevel.Debug,
        Message = "[PopupMenuHost] Dismiss canceled ({Kind}) by listener")]
    private static partial void LogDismissCancelled(ILogger logger, MenuDismissKind Kind);

    [Conditional("DEBUG")]
    private void LogDismissCancelled(MenuDismissKind kind)
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogDismissCancelled(logger, kind);
        }
    }

    [LoggerMessage(
        EventId = 3205,
        Level = LogLevel.Debug,
        Message = "[PopupMenuHost] Dismiss accepted ({Kind}); initiating popup close")]
    private static partial void LogDismissAccepted(ILogger logger, MenuDismissKind Kind);

    [Conditional("DEBUG")]
    private void LogDismissAccepted(MenuDismissKind kind)
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogDismissAccepted(logger, kind);
        }
    }

    [LoggerMessage(
        EventId = 3206,
        Level = LogLevel.Debug,
        Message = "[PopupMenuHost] Dismiss deferred ({Kind}); pending open for anchor {AnchorId}")]
    private static partial void LogDismissDeferred(ILogger logger, MenuDismissKind Kind, string AnchorId);

    [Conditional("DEBUG")]
    private void LogDismissDeferred(MenuDismissKind kind, FrameworkElement anchor)
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogDismissDeferred(logger, kind, GetAnchorId(anchor));
        }
    }

    [LoggerMessage(
        EventId = 3207,
        Level = LogLevel.Debug,
        Message = "[PopupMenuHost] Popup opened")]
    private static partial void LogPopupOpened(ILogger logger);

    [Conditional("DEBUG")]
    private void LogPopupOpened()
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogPopupOpened(logger);
        }
    }

    [LoggerMessage(
        EventId = 3208,
        Level = LogLevel.Debug,
        Message = "[PopupMenuHost] Popup closed ({Kind})")]
    private static partial void LogPopupClosed(ILogger logger, MenuDismissKind Kind);

    [Conditional("DEBUG")]
    private void LogPopupClosed(MenuDismissKind kind)
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogPopupClosed(logger, kind);
        }
    }

    [LoggerMessage(
        EventId = 3209,
        Level = LogLevel.Debug,
        Message = "[PopupMenuHost] Popup closed ({Kind}) ignored because anchor changed from {DismissedAnchorId} to {CurrentAnchorId}")]
    private static partial void LogPopupClosedIgnored(ILogger logger, MenuDismissKind Kind, string DismissedAnchorId, string CurrentAnchorId);

    [Conditional("DEBUG")]
    private void LogPopupClosedIgnored(MenuDismissKind kind, FrameworkElement? dismissedAnchor, FrameworkElement? currentAnchor)
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            var dismissedAnchorId = GetAnchorId(dismissedAnchor);
            var currentAnchorId = GetAnchorId(currentAnchor);
            LogPopupClosedIgnored(logger, kind, dismissedAnchorId, currentAnchorId);
        }
    }

    [LoggerMessage(
        EventId = 3210,
        Level = LogLevel.Debug,
        Message = "[PopupMenuHost] Pending open canceled ({Kind}) for anchor {AnchorId}")]
    private static partial void LogPendingOpenCancelled(ILogger logger, MenuDismissKind Kind, string AnchorId);

    [Conditional("DEBUG")]
    private void LogPendingOpenCancelled(MenuDismissKind kind, FrameworkElement anchor)
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogPendingOpenCancelled(logger, kind, GetAnchorId(anchor));
        }
    }

    [LoggerMessage(
        EventId = 3211,
        Level = LogLevel.Warning,
        Message = "[PopupMenuHost] ShowAt failed for anchor {AnchorId}: {ExceptionMessage}")]
    private static partial void LogShowAtFailed(ILogger logger, string AnchorId, string ExceptionMessage);

    private void LogShowAtFailed(MenuItem anchor, Exception ex)
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogShowAtFailed(logger, GetAnchorId(anchor), ex.Message);
        }
    }
}
