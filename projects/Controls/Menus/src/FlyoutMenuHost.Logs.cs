// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml.Controls.Primitives;

namespace DroidNet.Controls.Menus;

#pragma warning disable SA1204 // Static elements should appear before instance elements

/// <summary>
///     A <see cref="ICascadedMenuHost"/> implementation backed by <see cref="FlyoutBase"/>.
///     Acts as its own <see cref="ICascadedMenuSurface"/>.
/// </summary>
internal partial class FlyoutMenuHost
{
    [LoggerMessage(
        EventId = 3301,
        Level = LogLevel.Debug,
        Message = "[FlyoutMenuHost] flyout opening (MenuSource={HasMenuSource}, RootSurface={HasRootSurface})")]
    private static partial void LogOpening(ILogger logger, bool hasMenuSource, bool hasRootSurface);

    [Conditional("DEBUG")]
    private void LogOpening()
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            var hasMenuSource = this.MenuSource is not null;
            var hasRootSurface = this.RootSurface is not null;
            LogOpening(logger, hasMenuSource, hasRootSurface);
        }
    }

    [LoggerMessage(
        EventId = 3302,
        Level = LogLevel.Debug,
        Message = "[FlyoutMenuHost] flyout closing (Presenter={HasPresenter})")]
    private static partial void LogClosing(ILogger logger, bool hasPresenter);

    [Conditional("DEBUG")]
    private void LogClosing()
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            var hasPresenter = this.presenter is not null;
            LogClosing(logger, hasPresenter);
        }
    }

    [LoggerMessage(
        EventId = 3303,
        Level = LogLevel.Warning,
        Message = "[FlyoutMenuHost] flyout opening aborted: MenuSource is null")]
    private static partial void LogNoMenuSource(ILogger logger);

    private void LogNoMenuSource()
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogNoMenuSource(logger);
        }
    }

    [LoggerMessage(
        EventId = 3304,
        Level = LogLevel.Debug,
        Message = "[FlyoutMenuHost] Dismiss called (kind={Kind})")]
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
        EventId = 3305,
        Level = LogLevel.Debug,
        Message = "[FlyoutMenuHost] CreatePresenter called")]
    private static partial void LogCreatePresenter(ILogger logger);

    [Conditional("DEBUG")]
    private void LogCreatePresenter()
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogCreatePresenter(logger);
        }
    }

    [LoggerMessage(
        EventId = 3306,
        Level = LogLevel.Debug,
        Message = "[FlyoutMenuHost] flyout opened")]
    private static partial void LogOpened(ILogger logger);

    [LoggerMessage(
        EventId = 3307,
        Level = LogLevel.Debug,
        Message = "[FlyoutMenuHost] flyout opened (Anchor={AnchorId}, IsExpanded={IsExpanded})")]
    private static partial void LogOpened(ILogger logger, string anchorId, bool isExpanded);

    [Conditional("DEBUG")]
    private void LogOpened()
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            if (this.Anchor is MenuItem { ItemData: { } anchor })
            {
                LogOpened(logger, anchor.Id, anchor.IsExpanded);
            }
            else
            {
                LogOpened(logger);
            }
        }
    }
}
