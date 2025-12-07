// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace DroidNet.Controls;

/// <summary>
///     Logging helpers for <see cref="ToolBar"/>.
/// </summary>
public partial class ToolBar
{
    [LoggerMessage(EventId = 4001, Level = LogLevel.Trace, Message = "Overflow updated: {VisibleCount} visible, {OverflowCount} overflow")]
    private static partial void LogOverflowUpdated(ILogger logger, int visibleCount, int overflowCount);

    [Conditional("DEBUG")]
    private void LogOverflowUpdated(int visibleCount, int overflowCount)
    {
        if (this.logger is ILogger logger)
        {
            LogOverflowUpdated(logger, visibleCount, overflowCount);
        }
    }

    [LoggerMessage(EventId = 4002, Level = LogLevel.Trace, Message = "Primary Items changed: {Action}")]
    private static partial void LogPrimaryItemsChanged(ILogger logger, string action);

    [Conditional("DEBUG")]
    private void LogPrimaryItemsChanged(string action)
    {
        if (this.logger is ILogger logger)
        {
            LogPrimaryItemsChanged(logger, action);
        }
    }

    [LoggerMessage(EventId = 4003, Level = LogLevel.Trace, Message = "Secondary Items changed: {Action}")]
    private static partial void LogSecondaryItemsChanged(ILogger logger, string action);

    [Conditional("DEBUG")]
    private void LogSecondaryItemsChanged(string action)
    {
        if (this.logger is ILogger logger)
        {
            LogSecondaryItemsChanged(logger, action);
        }
    }

    [LoggerMessage(EventId = 4004, Level = LogLevel.Trace, Message = "IsCompact changed to {IsCompact}")]
    private static partial void LogIsCompactChanged(ILogger logger, bool isCompact);

    [Conditional("DEBUG")]
    private void LogIsCompactChanged(bool isCompact)
    {
        if (this.logger is ILogger logger)
        {
            LogIsCompactChanged(logger, isCompact);
        }
    }
}
