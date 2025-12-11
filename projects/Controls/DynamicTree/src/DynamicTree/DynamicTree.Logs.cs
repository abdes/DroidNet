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

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Tree got focus -> focusing selected item '{itemLabel}' (index {index})")]
    private static partial void LogFocusSelected(ILogger logger, string? itemLabel, int index);

    [Conditional("DEBUG")]
    private void LogFocusSelected(TreeItemAdapter item, int index)
    {
        if (this.logger is ILogger logger)
        {
            LogFocusSelected(logger, item.Label, index);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Tree got focus -> focusing fallback item '{itemLabel}'")]
    private static partial void LogFocusFallback(ILogger logger, string? itemLabel);

    [Conditional("DEBUG")]
    private void LogFocusFallback(ITreeItem item)
    {
        if (this.logger is ILogger logger)
        {
            LogFocusFallback(logger, item.Label);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Focus: item not found in shown items '{itemLabel}'")]
    private static partial void LogFocusIndexMissing(ILogger logger, string? itemLabel);

    [Conditional("DEBUG")]
    private void LogFocusIndexMissing(ITreeItem item)
    {
        if (this.logger is ILogger logger)
        {
            LogFocusIndexMissing(logger, item.Label);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Focus: element missing for '{itemLabel}' at index {index}")]
    private static partial void LogFocusElementMissing(ILogger logger, string? itemLabel, int index);

    [Conditional("DEBUG")]
    private void LogFocusElementMissing(ITreeItem item, int index)
    {
        if (this.logger is ILogger logger)
        {
            LogFocusElementMissing(logger, item.Label, index);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Focus: TryFocusAsync for '{itemLabel}' at index {index} target={targetType} succeeded={succeeded}")]
    private static partial void LogFocusResult(ILogger logger, string? itemLabel, int index, string targetType, bool succeeded);

    [Conditional("DEBUG")]
    private void LogFocusResult(ITreeItem item, int index, string targetType, bool succeeded)
    {
        if (this.logger is ILogger logger)
        {
            LogFocusResult(logger, item.Label, index, targetType, succeeded);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Focus: Direct Focus() for '{itemLabel}' at index {index} target={targetType} succeeded={succeeded}")]
    private static partial void LogFocusDirectResult(ILogger logger, string? itemLabel, int index, string targetType, bool succeeded);

    [Conditional("DEBUG")]
    private void LogFocusDirectResult(ITreeItem item, int index, string targetType, bool succeeded)
    {
        if (this.logger is ILogger logger)
        {
            LogFocusDirectResult(logger, item.Label, index, targetType, succeeded);
        }
    }
}
