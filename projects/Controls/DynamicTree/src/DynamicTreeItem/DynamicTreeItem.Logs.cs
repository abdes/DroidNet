// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace DroidNet.Controls;

/// <summary>
///     Logging helpers for <see cref="DynamicTreeItem"/>.
/// </summary>
public partial class DynamicTreeItem
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Item '{label}' (Depth={depth}) margin updated to ExtraLeftMargin={extraLeftMargin}.")]
    private static partial void LogItemMarginUpdated(ILogger logger, string label, int depth, double extraLeftMargin);

    [Conditional("DEBUG")]
    private void LogItemMarginUpdated(double extraLeftMargin)
    {
        if (!this.logger.IsEnabled(LogLevel.Trace))
        {
            return;
        }

        var itemAdapter = this.ItemAdapter;
        Debug.Assert(itemAdapter is not null, "ItemAdapter is null in LogItemMarginUpdated");
        var label = itemAdapter.Label;
        var depth = itemAdapter.Depth;
        LogItemMarginUpdated(this.logger, label, depth, extraLeftMargin);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Committing an in-place rename failed.")]
    private static partial void LogRenameFailed(ILogger logger, Exception exception);

    private void LogRenameFailed(Exception exception)
        => LogRenameFailed(this.logger, exception);
}
