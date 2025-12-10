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
        if (this.logger is ILogger logger)
        {
            var itemAdapter = this.ItemAdapter;
            Debug.Assert(itemAdapter is not null, "ItemAdapter is null in LogItemMarginUpdated");
            LogItemMarginUpdated(logger, itemAdapter.Label, itemAdapter.Depth, extraLeftMargin);
        }
    }
}
