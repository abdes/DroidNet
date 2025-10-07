// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

#pragma warning disable SA1204 // Static elements should appear before instance elements

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace DroidNet.Controls.Menus;

/// <summary>
///     Represents an individual menu item control, used within a <see cref="MenuBar"/> or <see cref="MenuFlyout"/>.
/// </summary>
public partial class MenuItem
{
    [LoggerMessage(
        EventId = 3501,
        Level = LogLevel.Error,
        Message = "[MenuItem] Command execution failed for `{ItemId}`")]
    private static partial void LogCommandExecutionFailed(ILogger logger, string itemId, Exception exception);

    private void LogCommandExecutionFailed(Exception ex)
    {
        if (this.MenuSource?.Services.MiscLogger is ILogger logger)
        {
            LogCommandExecutionFailed(logger, this.ItemData?.Id ?? "UNKNOWN", ex);
        }
    }

    [LoggerMessage(
        EventId = 3502,
        Level = LogLevel.Trace,
        Message = "[MenuItem] Transitioning `{ItemId}` to common state `{State}` (IsExpanded={IsExpanded})")]
    private static partial void LogVisualState(ILogger logger, string itemId, string state, bool isExpanded);

    [Conditional("DEBUG")]
    private void LogVisualState(string itemId, string state, bool isExpanded)
    {
        if (this.MenuSource?.Services.VisualLogger is ILogger logger)
        {
            LogVisualState(logger, itemId, state, isExpanded);
        }
    }
}
