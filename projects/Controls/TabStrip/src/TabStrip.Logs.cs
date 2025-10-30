// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Controls;

/// <summary>
///     A lightweight, reusable tab strip control for WinUI 3 that displays a dynamic row of tabs
///     and raises events or executes commands when tabs are invoked, selected, or closed.
/// </summary>
public partial class TabStrip
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

    // Logs pointer entered on a tab item
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Pointer entered tab item: {TabHeader}")]
    private static partial void LogPointerEntered(ILogger logger, string? TabHeader);

    [Conditional("DEBUG")]
    private void LogPointerEntered(object? sender)
    {
        if (this.logger is ILogger logger && sender is FrameworkElement fe && fe.DataContext is TabItem ti)
        {
            LogPointerEntered(logger, ti.Header);
        }
    }

    // Logs pointer exited on a tab item
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Pointer exited tab item: {TabHeader}")]
    private static partial void LogPointerExited(ILogger logger, string? TabHeader);

    [Conditional("DEBUG")]
    private void LogPointerExited(object? sender)
    {
        if (this.logger is ILogger logger && sender is FrameworkElement fe && fe.DataContext is TabItem ti)
        {
            LogPointerExited(logger, ti.Header);
        }
    }

    // Logs tab invoked
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Tab invoked: Header='{Header}', Index={Index}")]
    private static partial void LogTabInvoked(ILogger logger, string? Header, int Index);

    [Conditional("DEBUG")]
    private void LogTabInvoked(TabItem? item)
    {
        if (this.logger is ILogger logger && item is not null)
        {
            LogTabInvoked(logger, item.Header, this.SelectedIndex);
        }
    }

    // Logs selection changed
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Tab selection changed: OldHeader='{OldHeader}', NewHeader='{NewHeader}', OldIndex={OldIndex}, NewIndex={NewIndex}")]
    private static partial void LogSelectionChanged(ILogger logger, string? OldHeader, string? NewHeader, int OldIndex, int NewIndex);

    [Conditional("DEBUG")]
    private void LogSelectionChanged(TabItem? oldItem, TabItem? newItem, int oldIndex, int newIndex)
    {
        if (this.logger is ILogger logger)
        {
            LogSelectionChanged(logger, oldItem?.Header, newItem?.Header, oldIndex, newIndex);
        }
    }

    // Logs overflow left button click
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Overflow left button clicked")]
    private static partial void LogOverflowLeftClicked(ILogger logger);

    [Conditional("DEBUG")]
    private void LogOverflowLeftClicked()
    {
        if (this.logger is ILogger logger)
        {
            LogOverflowLeftClicked(logger);
        }
    }

    // Logs overflow right button click
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Overflow right button clicked")]
    private static partial void LogOverflowRightClicked(ILogger logger);

    [Conditional("DEBUG")]
    private void LogOverflowRightClicked()
    {
        if (this.logger is ILogger logger)
        {
            LogOverflowRightClicked(logger);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Other methods should follow the pattern of this pair of sample logging methods ({Foo}.")]
    private static partial void LogSample(ILogger logger, string foo);

    [Conditional("DEBUG")]
    private void LogSample(float oldValue, string foo)
    {
        if (this.logger is ILogger logger)
        {
            LogSample(logger, foo);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Tab item added: Header='{Header}', IsPinned={IsPinned}, IsClosable={IsClosable}, NewCount={Count}")]
    private static partial void LogItemAdded(ILogger logger, string Header, bool IsPinned, bool IsClosable, int Count);

    [Conditional("DEBUG")]
    private void LogItemAdded(TabItem? item, int count)
    {
        if (this.logger is ILogger logger && item is not null)
        {
            LogItemAdded(logger, item.Header ?? "<null>", item.IsPinned, item.IsClosable, count);
        }
    }

    // Logs template part not found
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Template part not found: {PartName}")]
    private static partial void LogTemplatePartNotFound(ILogger logger, string PartName);

    [Conditional("DEBUG")]
    private void LogTemplatePartNotFound(string partName)
    {
        if (this.logger is ILogger logger)
        {
            LogTemplatePartNotFound(logger, partName);
        }
    }

    // Logs tab sizing applied
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Tab sizing applied: Policy={Policy}, CalculatedWidth={CalculatedWidth}, MaxWidth={MaxWidth}, TabCount={TabCount}, AvailableWidth={AvailableWidth}")]
    private static partial void LogTabSizing(ILogger logger, TabWidthPolicy Policy, double? CalculatedWidth, double MaxWidth, int TabCount, double AvailableWidth);

    [Conditional("DEBUG")]
    private void LogTabSizing(TabWidthPolicy policy, double? calculatedWidth, double maxWidth, int tabCount, double availableWidth)
    {
        if (this.logger is ILogger logger)
        {
            LogTabSizing(logger, policy, calculatedWidth, maxWidth, tabCount, availableWidth);
        }
    }

    // Logs size changed
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "TabStrip size changed: NewWidth={NewWidth}, NewHeight={NewHeight}")]
    private static partial void LogSizeChanged(ILogger logger, double NewWidth, double NewHeight);

    [Conditional("DEBUG")]
    private void LogSizeChanged(double newWidth, double newHeight)
    {
        if (this.logger is ILogger logger)
        {
            LogSizeChanged(logger, newWidth, newHeight);
        }
    }

    // Logs unloaded
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "TabStrip unloaded")]
    private static partial void LogUnloaded(ILogger logger);

    [Conditional("DEBUG")]
    private void LogUnloaded()
    {
        if (this.logger is ILogger logger)
        {
            LogUnloaded(logger);
        }
    }

    // Logs bad item prepared
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Element prepared is not a valid TabStripItem: ElementType={ElementType}, DataContextType={DataContextType}")]
    private static partial void LogBadItem(ILogger logger, string ElementType, string DataContextType);

    [Conditional("DEBUG")]
    private void LogBadItem(ItemsRepeater sender, object element)
    {
        if (this.logger is ILogger logger)
        {
            var elementType = element?.GetType().Name ?? "null";
            var dataContextType = (element as FrameworkElement)?.DataContext?.GetType().Name ?? "null";
            LogBadItem(logger, elementType, dataContextType);
        }
    }

    // Logs setup prepared item
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Setting up prepared item: Header='{Header}', IsPinned={IsPinned}, Compacting={Compacting}, Bucket={Bucket}")]
    private static partial void LogSetupPreparedItem(ILogger logger, string Header, bool IsPinned, bool Compacting, string Bucket);

    [Conditional("DEBUG")]
    private void LogSetupPreparedItem(TabItem ti, bool compacting, bool pinned)
    {
        if (this.logger is ILogger logger)
        {
            LogSetupPreparedItem(logger, ti.Header ?? "<null>", ti.IsPinned, compacting, pinned ? "pinned" : "regular");
        }
    }

    // Logs clearing element
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Clearing element: Header='{Header}'")]
    private static partial void LogClearElement(ILogger logger, string Header);

    [Conditional("DEBUG")]
    private void LogClearElement(TabStripItem tsi)
    {
        if (this.logger is ILogger logger)
        {
            var header = (tsi.DataContext as TabItem)?.Header ?? "<null>";
            LogClearElement(logger, header);
        }
    }
}
