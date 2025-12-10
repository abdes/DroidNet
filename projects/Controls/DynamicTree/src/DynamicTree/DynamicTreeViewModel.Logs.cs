// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace DroidNet.Controls;

/// <summary>
///     Logging helpers for <see cref="Expander"/>.
/// </summary>
public partial class DynamicTreeViewModel
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Cannot expand item '{item}': item is not root, and its parent '{parent}' is not expanded")]
    private static partial void LogExpandItemNotVisible(ILogger logger, string item, string parent);

    private void LogExpandItemNotVisible(ITreeItem item)
        => LogExpandItemNotVisible(this.logger, item.Label, item.Parent?.Label ?? "<null>");

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Expanding item '{item}': Depth={depth}, Children={childrenCount}")]
    private static partial void LogExpandItem(ILogger logger, string item, int depth, int childrenCount);

    [Conditional("DEBUG")]
    private void LogExpandItem(ITreeItem item)
        => LogExpandItem(this.logger, item.Label, item.Depth, item.ChildrenCount);
}
