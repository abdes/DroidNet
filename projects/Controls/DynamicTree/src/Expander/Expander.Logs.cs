// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace DroidNet.Controls;

/// <summary>
///     Logging helpers for <see cref="Expander"/>.
/// </summary>
public partial class Expander
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "[Expander] toggled from {before} to {after}.")]
    private static partial void LogToggle(ILogger logger, string before, string after);

    [Conditional("DEBUG")]
    private void LogToggle()
    {
        if (this.logger is ILogger logger)
        {
            LogToggle(logger, this.IsExpanded ? ExpandedVisualState : CollapsedVisualState, this.IsExpanded ? CollapsedVisualState : ExpandedVisualState);
        }
    }
}
