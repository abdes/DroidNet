// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;
using Windows.System;

namespace DroidNet.Controls;

/// <summary>
///     Logging helpers for <see cref="DynamicTree"/>.
/// </summary>
public partial class DynamicTree
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Key down: {key}")]
    private static partial void LogKeyDown(ILogger logger, VirtualKey key);

    [Conditional("DEBUG")]
    private void LogKeyDown(VirtualKey key)
    {
        if (this.logger is ILogger logger)
        {
            LogKeyDown(logger, key);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Double tapped: {source}")]
    private static partial void LogDoubleTapped(ILogger logger, string source);

    [Conditional("DEBUG")]
    private void LogDoubleTapped(object source)
    {
        if (this.logger is ILogger logger)
        {
            LogDoubleTapped(logger, source.ToString() ?? "<null>");
        }
    }
}
