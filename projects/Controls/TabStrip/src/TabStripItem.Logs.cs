// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace DroidNet.Controls;

/// <summary>
///     Represents a single tab item in a TabStrip control.
/// </summary>
public partial class TabStripItem
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "TabStripItem pointer entered.")]
    private static partial void LogPointerEntered(ILogger logger);

    [Conditional("DEBUG")]
    private void LogPointerEntered()
    {
        if (this.logger is ILogger logger)
        {
            LogPointerEntered(logger);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "TabStripItem pointer exited.")]
    private static partial void LogPointerExited(ILogger logger);

    [Conditional("DEBUG")]
    private void LogPointerExited()
    {
        if (this.logger is ILogger logger)
        {
            LogPointerExited(logger);
        }
    }
}
