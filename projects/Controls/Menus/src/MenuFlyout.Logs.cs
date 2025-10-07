// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace DroidNet.Controls.Menus;

#pragma warning disable SA1204 // Static elements should appear before instance elements

/// <summary>
///     Custom flyout surface that renders menu data using <see cref="ColumnPresenter"/> columns.
///     This implementation keeps interaction logic reusable across menu containers via
///     <see cref="MenuInteractionController"/>.
/// </summary>
public partial class MenuFlyout
{
    [LoggerMessage(
        EventId = 3301,
        Level = LogLevel.Debug,
        Message = "[MenuFlyout] flyout opening (MenuSource={HasMenuSource}, RootSurface={HasRootSurface})")]
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
        Message = "[MenuFlyout] flyout closing (Presenter={HasPresenter})")]
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
        Message = "[MenuFlyout] flyout opening aborted: MenuSource is null")]
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
        Message = "[MenuFlyout] Dismiss called (kind={Kind})")]
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
        Message = "[MenuFlyout] CreatePresenter called")]
    private static partial void LogCreatePresenter(ILogger logger);

    [Conditional("DEBUG")]
    private void LogCreatePresenter()
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogCreatePresenter(logger);
        }
    }
}
