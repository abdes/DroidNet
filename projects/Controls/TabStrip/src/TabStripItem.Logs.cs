// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Input;

namespace DroidNet.Controls;

/// <summary>
///     Represents a single tab item in a TabStrip control.
/// </summary>
public partial class TabStripItem
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Applying template...")]
    private static partial void LogApplyingTemplate(ILogger logger);

    [Conditional("DEBUG")]
    private void LogApplyingTemplate()
    {
        if (this.logger is ILogger logger)
        {
            LogApplyingTemplate(logger);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Template part '{PartName}' (required={IsRequired}) of type '{ExpectedType}' not found.")]
    private static partial void LogTemplatePartNotFound(ILogger logger, string partName, string expectedType, bool isRequired);

    private void LogTemplatePartNotFound(string partName, Type expectedType, bool isRequired)
    {
        if (this.logger is ILogger logger)
        {
            LogTemplatePartNotFound(logger, partName, expectedType.Name, isRequired);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "{State}{Detail}")]
    private static partial void LogEnabledState(ILogger logger, string state, string detail);

    [Conditional("DEBUG")]
    private void LogEnabledOrDisabled()
    {
        if (this.logger is ILogger logger)
        {
            LogEnabledState(
                logger,
                this.IsEnabled ? "Enabled" : "Disabled",
                this.Item is null ? " because TabItem is null" : $", TabItem is set with '{this.Item.Header}'");
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Pointer entered at '{{X={X}, Y={Y}}}', (IsCompact={IsCompact})")]
    private static partial void LogPointerEntered(ILogger logger, double x, double y, bool isCompact);

    [Conditional("DEBUG")]
    private void LogPointerEntered(PointerRoutedEventArgs e)
    {
        if (this.logger is ILogger logger)
        {
            var pos = e.GetCurrentPoint(this).Position;
            LogPointerEntered(logger, pos.X, pos.Y, this.IsCompact);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Pointer exited, (IsCompact={IsCompact})")]
    private static partial void LogPointerExited(ILogger logger, bool isCompact);

    [Conditional("DEBUG")]
    private void LogPointerExited()
    {
        if (this.logger is ILogger logger)
        {
            LogPointerExited(logger, this.IsCompact);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Visual state changed to '{State}' (IsCompact={IsCompact}, PointerOver={PointerOver}, IsSelected={Selected}, IsPinned={Pinned})")]
    private static partial void LogVisualState(ILogger logger, string state, bool isCompact, bool pointerOver, bool selected, bool pinned);

    [Conditional("DEBUG")]
    private void LogVisualState(string state)
    {
        if (this.logger is ILogger logger && this.Item is { } item)
        {
            LogVisualState(logger, state, this.IsCompact, this.isPointerOver, item.IsSelected, item.IsPinned);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Item changed from '{OldValue}' to '{NewValue}'")]
    private static partial void LogItemChanged(ILogger logger, string oldValue, string newValue);

    [Conditional("DEBUG")]
    private void LogItemChanged(DependencyPropertyChangedEventArgs e)
    {
        if (this.logger is ILogger logger)
        {
            var oldValueStr = e.OldValue is TabItem oldTab ? oldTab.Header ?? "null" : "null";
            var newValueStr = e.NewValue is TabItem newTab ? newTab.Header ?? "null" : "null";
            LogItemChanged(logger, oldValueStr, newValueStr);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "TabItem property '{PropertyName}' changed to '{NewValue}'")]
    private static partial void LogItemPropertyChanged(ILogger logger, string propertyName, object newValue);

    [Conditional("DEBUG")]
    private void LogItemPropertyChanged(System.ComponentModel.PropertyChangedEventArgs e)
    {
        if (this.logger is ILogger logger && this.Item is { } item)
        {
            var property = item.GetType().GetProperty(e.PropertyName!);
            var newValue = property?.GetValue(item) ?? (object)"null";
            LogItemPropertyChanged(logger, e.PropertyName!, newValue);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Pin button clicked, item is now {Pinned}")]
    private static partial void LogPinClicked(ILogger logger, string pinned);

    [Conditional("DEBUG")]
    private void LogPinClicked()
    {
        if (this.logger is ILogger logger)
        {
            LogPinClicked(logger, this.Item is { IsPinned: true } ? "pinned" : "unpinned");
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Close button clicked, event CloseRequested invoked for item '{ItemHeader}'")]
    private static partial void LogCloseRequested(ILogger logger, string itemHeader);

    [Conditional("DEBUG")]
    private void LogCloseRequested()
    {
        if (this.logger is ILogger logger)
        {
            LogCloseRequested(logger, this.Item?.Header ?? "<null>");
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "MinWidth updated to {MinWidth}")]
    private static partial void LogMinWidthUpdated(ILogger logger, double minWidth);

    [Conditional("DEBUG")]
    private void LogMinWidthUpdated()
    {
        if (this.logger is ILogger logger)
        {
            LogMinWidthUpdated(logger, this.MinWidth);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Compact mode changed from {OldValue} to {NewValue}")]
    private static partial void LogCompactModeChanged(ILogger logger, bool oldValue, bool newValue);

    [Conditional("DEBUG")]
    private void LogCompactModeChanged(bool oldValue, bool newValue)
    {
        if (this.logger is ILogger logger)
        {
            LogCompactModeChanged(logger, oldValue, newValue);
        }
    }
}
