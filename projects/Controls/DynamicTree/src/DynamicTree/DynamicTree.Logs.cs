// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Input;
using Windows.System;
using static DroidNet.Controls.DynamicTreeViewModel;

namespace DroidNet.Controls;

/// <summary>
///     Logging helpers for <see cref="DynamicTree"/>.
/// </summary>
public partial class DynamicTree
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "The dynamic tree rejected a drop request.")]
    private static partial void LogDropRejected(ILogger logger, Exception exception);

    private void LogDropRejected(Exception exception)
        => LogDropRejected(this.logger, exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Key down: {key}")]
    private static partial void LogKeyDown(ILogger logger, VirtualKey key);

    [Conditional("DEBUG")]
    private void LogKeyDown(VirtualKey key)
        => LogKeyDown(this.logger, key);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Tree Control got focus")]
    private static partial void LogGotFocus(ILogger logger);

    [Conditional("DEBUG")]
    private void LogGotFocus()
        => LogGotFocus(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Tree Control double tapped")]
    private static partial void LogDoubleTapped(ILogger logger);

    [Conditional("DEBUG")]
    private void LogDoubleTapped()
        => LogDoubleTapped(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Element prepared: {elementType} ItemLabel='{itemLabel}'")]
    private static partial void LogElementPrepared(ILogger logger, string elementType, string? itemLabel);

    [Conditional("DEBUG")]
    private void LogElementPrepared(FrameworkElement? element)
    {
        if (!this.logger.IsEnabled(LogLevel.Trace))
        {
            return;
        }

        var elementType = element?.GetType().Name ?? "<unknown>";
        var itemLabel = LabelOf(element);
        LogElementPrepared(this.logger, elementType, itemLabel);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Element clearing: {elementType} ItemLabel='{itemLabel}'")]
    private static partial void LogElementClearing(ILogger logger, string elementType, string? itemLabel);

    [Conditional("DEBUG")]
    private void LogElementClearing(FrameworkElement? element)
    {
        if (!this.logger.IsEnabled(LogLevel.Trace))
        {
            return;
        }

        var elementType = element?.GetType().Name ?? "<unknown>";
        var itemLabel = LabelOf(element);
        LogElementClearing(this.logger, elementType, itemLabel);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "DisplayedItems source: isFilteringEnabled={isFilteringEnabled}, hasPredicate={hasPredicate} -> {displayedSource}")]
    private static partial void LogDisplayedItemsSource(ILogger logger, bool isFilteringEnabled, bool hasPredicate, string displayedSource);

    [Conditional("DEBUG")]
    private void LogDisplayedItemsSource(bool hasPredicate, bool useFiltered)
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        var isFilteringEnabled = this.IsFilteringEnabled;
        var displayedSource = useFiltered ? nameof(DynamicTreeViewModel.FilteredItems) : nameof(DynamicTreeViewModel.ShownItems);
        LogDisplayedItemsSource(this.logger, isFilteringEnabled, hasPredicate, displayedSource);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Focus: item not found in shown items '{itemLabel}'")]
    private static partial void LogFocusIndexMissing(ILogger logger, string? itemLabel);

    [Conditional("DEBUG")]
    private void LogFocusIndexMissing(ITreeItem item)
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        var itemLabel = item.Label;
        LogFocusIndexMissing(this.logger, itemLabel);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Focus: element missing for '{itemLabel}' at index {index}")]
    private static partial void LogFocusElementMissing(ILogger logger, string? itemLabel, int index);

    [Conditional("DEBUG")]
    private void LogFocusElementMissing(ITreeItem item, int index)
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        var itemLabel = item.Label;
        LogFocusElementMissing(this.logger, itemLabel, index);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Focus: TryFocusAsync for '{itemLabel}' at index {index} target={targetType} succeeded={succeeded}")]
    private static partial void LogFocusResult(ILogger logger, string? itemLabel, int index, string targetType, bool succeeded);

    [Conditional("DEBUG")]
    private void LogFocusResult(ITreeItem item, int index, DependencyObject target, bool succeeded)
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        var itemLabel = item.Label;
        var targetType = target.GetType().Name;
        LogFocusResult(this.logger, itemLabel, index, targetType, succeeded);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Pointer pressed: Item='{itemLabel}' OriginalSource={sourceType} Device={deviceType} LeftPressed={leftPressed} Handled={handled}")]
    private static partial void LogPointerPressed(ILogger logger, string? itemLabel, string? sourceType, string? deviceType, bool leftPressed, bool handled);

    [Conditional("DEBUG")]
    private void LogPointerPressed(FrameworkElement element, PointerRoutedEventArgs args)
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        var itemLabel = LabelOf(element);
        var sourceType = args.OriginalSource?.GetType().Name ?? "<unknown>";
        var deviceType = args.Pointer?.PointerDeviceType.ToString() ?? "<unknown>";
        var leftPressed = args.GetCurrentPoint(element).Properties.IsLeftButtonPressed;
        var handled = args.Handled;
        LogPointerPressed(this.logger, itemLabel, sourceType, deviceType, leftPressed, handled);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Tapped: Item='{itemLabel}' Source={sourceType} Handled={handled}")]
    private static partial void LogTapped(ILogger logger, string? itemLabel, string? sourceType, bool handled);

    [Conditional("DEBUG")]
    private void LogTapped(FrameworkElement element, TappedRoutedEventArgs args)
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        var itemLabel = LabelOf(element);
        var sourceType = args.OriginalSource?.GetType().Name ?? "<unknown>";
        var handled = args.Handled;
        LogTapped(this.logger, itemLabel, sourceType, handled);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Focus: Apply attempt for '{itemLabel}' at index {index} state={state} pending={pending}")]
    private static partial void LogFocusApplyAttempt(ILogger logger, string? itemLabel, int index, string state, bool pending);

    [Conditional("DEBUG")]
    private void LogFocusApplyAttempt(ITreeItem item, int index, FocusState state, bool pending)
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        var itemLabel = item.Label;
        var focusState = state.ToString();
        LogFocusApplyAttempt(this.logger, itemLabel, index, focusState, pending);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Item='{itemLabel}' got focus (Last Origin={origin})")]
    private static partial void LogItemGotFocus(ILogger logger, string? itemLabel, RequestOrigin origin);

    [Conditional("DEBUG")]
    private void LogItemGotFocus(TreeItemAdapter? item, RequestOrigin origin)
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        var itemLabel = item?.Label;
        LogItemGotFocus(this.logger, itemLabel, origin);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Run queued focus: Item='{itemLabel}' pending={pending} state={state}")]
    private static partial void LogApplyFocus(ILogger logger, string? itemLabel, bool pending, string state);

    [Conditional("DEBUG")]
    private void LogApplyFocus(ITreeItem? item, bool pending, FocusState state)
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        var itemLabel = item?.Label;
        var focusState = state.ToString();
        LogApplyFocus(this.logger, itemLabel, pending, focusState);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Focused item reported by ViewModel is null")]
    private static partial void LogFocusLost(ILogger logger);

    [Conditional("DEBUG")]
    private void LogFocusLost()
        => LogFocusLost(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Enqueue focus application on UI dispatcher: item '{Item}', origin={Origin}")]
    private static partial void LogEnqueueApplyFocus(ILogger logger, string item, RequestOrigin origin);

    [Conditional("DEBUG")]
    private void LogEnqueueApplyFocus()
    {
        if (!this.logger.IsEnabled(LogLevel.Debug) || this.ViewModel?.FocusedItem is not { } focused)
        {
            return;
        }

        var itemLabel = focused.Item.Label;
        var origin = focused.Origin;
        LogEnqueueApplyFocus(this.logger, itemLabel, origin);
    }

    /// <summary>Gets the label of the item a row shows, when the element is a row.</summary>
    /// <param name="element">The element, if any.</param>
    /// <returns>The row's item label, or <see langword="null"/>.</returns>
    private static string? LabelOf(FrameworkElement? element)
        => element is DynamicTreeItem { ItemAdapter: TreeItemAdapter adapter } ? adapter.Label : null;
}
