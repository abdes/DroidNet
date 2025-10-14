// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using Microsoft.Extensions.Logging;

namespace DroidNet.Controls.Menus;

/// <content>
///     Logging helpers for <see cref="ExpandableMenuBar"/>.
/// </content>
public sealed partial class ExpandableMenuBar
{
    [LoggerMessage(
        EventId = 3301,
        Level = LogLevel.Debug,
        Message = "[ExpandableMenuBar] Expanding, source={Source}")]
    private static partial void LogExpanding(ILogger logger, MenuInteractionInputSource source);

    [LoggerMessage(
        EventId = 3302,
        Level = LogLevel.Debug,
        Message = "[ExpandableMenuBar] Collapsing, kind={Kind}")]
    private static partial void LogCollapsing(ILogger logger, MenuDismissKind kind);

    [LoggerMessage(
        EventId = 3303,
        Level = LogLevel.Debug,
        Message = "[ExpandableMenuBar] Root items collection changed: action={Action}")]
    private static partial void LogRootItemsChanged(ILogger logger, NotifyCollectionChangedAction action);

    private void LogExpandingState(MenuInteractionInputSource source)
    {
        if (this.MenuSource?.Services.InteractionLogger is ILogger logger)
        {
            LogExpanding(logger, source);
        }
    }

    private void LogCollapsingState(MenuDismissKind kind)
    {
        if (this.MenuSource?.Services.InteractionLogger is ILogger logger)
        {
            LogCollapsing(logger, kind);
        }
    }

    private void LogRootItemsStateChange(NotifyCollectionChangedAction action)
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogRootItemsChanged(logger, action);
        }
    }
}
