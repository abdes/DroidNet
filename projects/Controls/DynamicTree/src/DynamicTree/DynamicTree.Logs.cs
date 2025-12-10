// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;
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

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Element prepared: {elementType} ItemLabel='{itemLabel}'")]
    private static partial void LogElementPrepared(ILogger logger, string elementType, string? itemLabel);

    [Conditional("DEBUG")]
    private void LogElementPrepared(FrameworkElement? element)
    {
        if (this.logger is not ILogger logger)
        {
            return;
        }

        var elementType = element?.GetType().Name ?? "<unknown>";
        string? itemLabel = null;
        if (element is DynamicTreeItem dtItem && dtItem.ItemAdapter is TreeItemAdapter adapter)
        {
            itemLabel = adapter.Label;
        }

        LogElementPrepared(logger, elementType, itemLabel);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Element clearing: {elementType} ItemLabel='{itemLabel}'")]
    private static partial void LogElementClearing(ILogger logger, string elementType, string? itemLabel);

    [Conditional("DEBUG")]
    private void LogElementClearing(FrameworkElement? element)
    {
        if (this.logger is not ILogger logger)
        {
            return;
        }

        var elementType = element?.GetType().Name ?? "<unknown>";
        string? itemLabel = null;
        if (element is DynamicTreeItem dtItem && dtItem.ItemAdapter is TreeItemAdapter adapter)
        {
            itemLabel = adapter.Label;
        }

        LogElementClearing(logger, elementType, itemLabel);
    }
}
