// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace DroidNet.Controls;

/// <summary>
///     Logging helpers for <see cref="ToolBarToggleButton"/>.
/// </summary>
public partial class ToolBarToggleButton
{
    [LoggerMessage(EventId = 4201, Level = LogLevel.Trace, Message = "[ToolBarToggleButton] Label position updated to {Position} (IsLabelVisible={IsLabelVisible})")]
    private static partial void LogLabelPositionUpdated(ILogger logger, string position, bool isLabelVisible);

    [Conditional("DEBUG")]
    private void LogLabelPositionUpdated(string position, bool isLabelVisible)
    {
        if (this.logger is ILogger logger)
        {
            LogLabelPositionUpdated(logger, position, isLabelVisible);
        }
    }

    [LoggerMessage(EventId = 4202, Level = LogLevel.Trace, Message = "[ToolBarToggleButton] Visual state updated to {State}")]
    private static partial void LogVisualStateUpdated(ILogger logger, string state);

    [Conditional("DEBUG")]
    private void LogVisualStateUpdated(string state)
    {
        if (this.logger is ILogger logger)
        {
            LogVisualStateUpdated(logger, state);
        }
    }
}
