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
        Message = "[{Header}] Applying template...")]
    private static partial void LogApplyingTemplate(ILogger logger, string header);

    [Conditional("DEBUG")]
    private void LogApplyingTemplate()
    {
        if (this.logger is ILogger logger)
        {
            LogApplyingTemplate(logger, this.Item?.Header ?? "<null>");
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "[{Header}] Template part '{PartName}' (required={IsRequired}) of type '{ExpectedType}' not found.")]
    private static partial void LogTemplatePartNotFound(ILogger logger, string header, string partName, string expectedType, bool isRequired);

    private void LogTemplatePartNotFound(string partName, Type expectedType, bool isRequired)
    {
        if (this.logger is ILogger logger)
        {
            LogTemplatePartNotFound(logger, this.Item?.Header ?? "<null>", partName, expectedType.Name, isRequired);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "[{Header}] {State}{Detail}")]
    private static partial void LogEnabledState(ILogger logger, string header, string state, string detail);

    [Conditional("DEBUG")]
    private void LogEnabledOrDisabled()
    {
        if (this.logger is ILogger logger)
        {
            LogEnabledState(
                logger,
                this.Item?.Header ?? "<null>",
                this.IsEnabled ? "Enabled" : "Disabled",
                this.Item is null ? " because TabItem is null" : $", TabItem is set with '{this.Item.Header}'");
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "[{Header}] Pointer entered at '{{X={X}, Y={Y}}}', (IsCompact={IsCompact})")]
    private static partial void LogPointerEntered(ILogger logger, string header, double x, double y, bool isCompact);

    [Conditional("DEBUG")]
    private void LogPointerEntered(PointerRoutedEventArgs e)
    {
        if (this.logger is ILogger logger)
        {
            var pos = e.GetCurrentPoint(this).Position;
            LogPointerEntered(logger, this.Item?.Header ?? "<null>", pos.X, pos.Y, this.IsCompact);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "[{Header}] Pointer exited, (IsCompact={IsCompact})")]
    private static partial void LogPointerExited(ILogger logger, string header, bool isCompact);

    [Conditional("DEBUG")]
    private void LogPointerExited()
    {
        if (this.logger is ILogger logger)
        {
            LogPointerExited(logger, this.Item?.Header ?? "<null>", this.IsCompact);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "[{Header}] Visual state changed to '{State}' (IsCompact={IsCompact}, PointerOver={PointerOver}, IsSelected={Selected}, IsPinned={Pinned})")]
    private static partial void LogVisualState(ILogger logger, string header, string state, bool isCompact, bool pointerOver, bool selected, bool pinned);

    [Conditional("DEBUG")]
    private void LogVisualState(string state)
    {
        if (this.logger is ILogger logger && this.Item is { } item)
        {
            LogVisualState(logger, this.Item?.Header ?? "<null>", state, this.IsCompact, this.isPointerOver, item.IsSelected, item.IsPinned);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "[{Header}] Item changed from '{OldValue}' to '{NewValue}'")]
    private static partial void LogItemChanged(ILogger logger, string header, string oldValue, string newValue);

    [Conditional("DEBUG")]
    private void LogItemChanged(DependencyPropertyChangedEventArgs e)
    {
        if (this.logger is ILogger logger)
        {
            var oldValueStr = e.OldValue is TabItem oldTab ? oldTab.Header ?? "null" : "null";
            var newValueStr = e.NewValue is TabItem newTab ? newTab.Header ?? "null" : "null";
            LogItemChanged(logger, newValueStr, oldValueStr, newValueStr);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "[{Header}] TabItem property '{PropertyName}' changed to '{NewValue}'")]
    private static partial void LogItemPropertyChanged(ILogger logger, string header, string propertyName, object newValue);

    [Conditional("DEBUG")]
    private void LogItemPropertyChanged(System.ComponentModel.PropertyChangedEventArgs e)
    {
        if (this.logger is ILogger logger && this.Item is { } item)
        {
            var property = item.GetType().GetProperty(e.PropertyName!);
            var newValue = property?.GetValue(item) ?? (object)"null";
            LogItemPropertyChanged(logger, this.Item?.Header ?? "<null>", e.PropertyName!, newValue);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "[{Header}] Pin button clicked, item is now {Pinned}")]
    private static partial void LogPinClicked(ILogger logger, string header, string pinned);

    [Conditional("DEBUG")]
    private void LogPinClicked()
    {
        if (this.logger is ILogger logger)
        {
            LogPinClicked(logger, this.Item?.Header ?? "<null>", this.Item is { IsPinned: true } ? "pinned" : "unpinned");
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "[{Header}] Close button clicked, event CloseRequested invoked")]
    private static partial void LogCloseRequested(ILogger logger, string header);

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
        Message = "[{Header}] MinWidth updated to {MinWidth}")]
    private static partial void LogMinWidthUpdated(ILogger logger, string header, double minWidth);

    [Conditional("DEBUG")]
    private void LogMinWidthUpdated()
    {
        if (this.logger is ILogger logger)
        {
            LogMinWidthUpdated(logger, this.Item?.Header ?? "<null>", this.MinWidth);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "[{Header}] Compact mode changed from {OldValue} to {NewValue}")]
    private static partial void LogCompactModeChanged(ILogger logger, string header, bool oldValue, bool newValue);

    [Conditional("DEBUG")]
    private void LogCompactModeChanged(bool oldValue, bool newValue)
    {
        if (this.logger is ILogger logger)
        {
            LogCompactModeChanged(logger, this.Item?.Header ?? "<null>", oldValue, newValue);
        }
    }
}
