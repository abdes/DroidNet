// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Input;
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

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Pointer pressed: Item='{itemLabel}' OriginalSource={sourceType} Device={deviceType} LeftPressed={leftPressed} Handled={handled}")]
    private static partial void LogPointerPressed(ILogger logger, string? itemLabel, string? sourceType, string? deviceType, bool leftPressed, bool handled);

    [Conditional("DEBUG")]
    private void LogPointerPressed(FrameworkElement element, PointerRoutedEventArgs args)
    {
        if (this.logger is not ILogger logger)
        {
            return;
        }

        string? itemLabel = null;
        if (element is DynamicTreeItem dtItem && dtItem.ItemAdapter is TreeItemAdapter adapter)
        {
            itemLabel = adapter.Label;
        }

        var sourceType = args.OriginalSource?.GetType().Name ?? "<unknown>";
        var deviceType = args.Pointer?.PointerDeviceType.ToString() ?? "<unknown>";
        var leftPressed = args.GetCurrentPoint(element).Properties.IsLeftButtonPressed;
        var handled = args.Handled;

        LogPointerPressed(logger, itemLabel, sourceType, deviceType, leftPressed, handled);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Tapped: Item='{itemLabel}' Source={sourceType} Handled={handled}")]
    private static partial void LogTapped(ILogger logger, string? itemLabel, string? sourceType, bool handled);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Focus: Apply attempt for '{itemLabel}' at index {index} state={state} isApplying={isApplying} pending={pending}")]
    private static partial void LogFocusApplyAttempt(ILogger logger, string? itemLabel, int index, string state, bool isApplying, bool pending);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Requesting model focus: Item='{itemLabel}' Origin={origin} ForceRaise={forceRaise}")]
    private static partial void LogRequestModelFocus(ILogger logger, string? itemLabel, string origin, bool forceRaise);

    [Conditional("DEBUG")]
    private void LogRequestModelFocusWrapper(TreeItemAdapter? item, FocusRequestOrigin origin, bool forceRaise)
    {
        if (this.logger is not ILogger logger)
        {
            return;
        }

        LogRequestModelFocus(logger, item?.Label, origin.ToString(), forceRaise);
    }

    [Conditional("DEBUG")]
    private void LogFocusApplyAttempt(ITreeItem item, int index, FocusState state, bool isApplying, bool pending)
    {
        if (this.logger is not ILogger logger)
        {
            return;
        }

        LogFocusApplyAttempt(logger, item.Label, index, state.ToString(), isApplying, pending);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Run queued focus: Item='{itemLabel}' pending={pending} state={state}")]
    private static partial void LogRunTryFocus(ILogger logger, string? itemLabel, bool pending, string state);

    [Conditional("DEBUG")]
    private void LogRunTryFocus(ITreeItem? item, bool pending, FocusState state)
    {
        if (this.logger is not ILogger logger)
        {
            return;
        }

        LogRunTryFocus(logger, item?.Label, pending, state.ToString());
    }

    [Conditional("DEBUG")]
    private void LogTapped(FrameworkElement element, TappedRoutedEventArgs args)
    {
        if (this.logger is not ILogger logger)
        {
            return;
        }

        string? itemLabel = null;
        if (element is DynamicTreeItem dtItem && dtItem.ItemAdapter is TreeItemAdapter adapter)
        {
            itemLabel = adapter.Label;
        }

        var sourceType = args.OriginalSource?.GetType().Name ?? "<unknown>";
        var handled = args.Handled;
        LogTapped(logger, itemLabel, sourceType, handled);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "GotFocus event: Item='{itemLabel}' Origin={origin} ApplyingFocus={applying} Handled={handled}")]
    private static partial void LogGotFocusEvent(ILogger logger, string? itemLabel, string origin, bool applying, bool handled);

    [Conditional("DEBUG")]
    private void LogGotFocusEvent(FrameworkElement element, FocusRequestOrigin origin, bool applying, RoutedEventArgs args)
    {
        if (this.logger is not ILogger logger)
        {
            return;
        }

        string? itemLabel = null;
        if (element is DynamicTreeItem dtItem && dtItem.ItemAdapter is TreeItemAdapter adapter)
        {
            itemLabel = adapter.Label;
        }

        // RoutedEventArgs in WinUI does not expose a 'Handled' property; use false for logging.
        LogGotFocusEvent(logger, itemLabel, origin.ToString(), applying, handled: false);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "ViewModel.PropertyChanged: {property} -> FocusedItem = {focusedLabel} Origin={origin} EnqueueFocus={enqueue}")]
    private static partial void LogViewModelPropertyChanged(ILogger logger, string? property, string? focusedLabel, string origin, bool enqueue);

    [Conditional("DEBUG")]
    private void LogViewModelPropertyChanged(string? property, ITreeItem? focusedItem, FocusRequestOrigin origin, bool enqueue)
    {
        if (this.logger is not ILogger logger)
        {
            return;
        }

        var label = focusedItem?.Label;
        LogViewModelPropertyChanged(logger, property, label, origin.ToString(), enqueue);
    }
}
