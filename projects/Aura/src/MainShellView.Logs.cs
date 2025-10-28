// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Globalization;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;
using Windows.Graphics;

namespace DroidNet.Aura;

/// <summary>
///     Represents the main shell view of the application, providing the main user interface and
///     handling window-related events.
/// </summary>
public partial class MainShellView
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "View loaded with Viewmodel '{ViewModel}'")]
    private static partial void LogLoaded(ILogger logger, string viewModel);

    [Conditional("DEBUG")]
    private void LogLoaded()
    {
        Debug.Assert(this.IsLoaded, "logging can only be done after view is loaded");
        LogLoaded(this.logger, this.ViewModel?.GetType().Name ?? "<null>");
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "View unloaded")]
    private static partial void LogUnloaded(ILogger logger);

    [Conditional("DEBUG")]
    private void LogUnloaded()
        => LogUnloaded(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "'{TypeName}' subscribed to titlebar layout observables")]
    private static partial void LogObservablesSubscribed(ILogger logger, string typeName);

    [Conditional("DEBUG")]
    private void LogObservablesSubscribed()
    {
        Debug.Assert(this.IsLoaded, "logging can only be done after view is loaded");
        LogObservablesSubscribed(this.logger, nameof(MainShellView));
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Throttled titlebar event: {EventType}")]
    private static partial void LogThrottledTitlebarEvent(ILogger logger, string eventType);

    [Conditional("DEBUG")]
    private void LogThrottledTitlebarEvent(string eventType)
    {
        Debug.Assert(this.IsLoaded, "logging can only be done after view is loaded");
        LogThrottledTitlebarEvent(this.logger, eventType);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "View logger initialized/replaced")]
    private static partial void LogLoggerInitialized(ILogger logger);

    [Conditional("DEBUG")]
    private void LogLoggerInitialized()
    {
        Debug.Assert(this.IsLoaded, "logging can only be done after view is loaded");
        LogLoggerInitialized(this.logger);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "SetupCustomTitleBar invoked for window '{WindowTitle}'")]
    private static partial void LogSetupCustomTitleBarInvoked(ILogger logger, string windowTitle);

    [Conditional("DEBUG")]
    private void LogSetupCustomTitleBarInvoked()
    {
        Debug.Assert(this.IsLoaded, "logging can only be done after view is loaded");
        LogSetupCustomTitleBarInvoked(this.logger, this.ViewModel?.Window?.Title ?? "<bad vm or window>");
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Computed MinWindowWidth={MinWidth:N1}")]
    private static partial void LogSetupCustomTitleBarMinWidth(ILogger logger, double minWidth);

    [Conditional("DEBUG")]
    private void LogSetWindowMinWidth()
    {
        Debug.Assert(this.IsLoaded, "logging can only be done after view is loaded");
        LogSetupCustomTitleBarMinWidth(this.logger, this.MinWindowWidth);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Passthrough element skipped: '{ElementId}' (not visible or zero size)")]
    private static partial void LogPassthroughElementSkipped(ILogger logger, string elementId);

    [Conditional("DEBUG")]
    private void LogPassthroughElementSkipped(string elementId)
    {
        Debug.Assert(this.IsLoaded, "logging can only be done after view is loaded");
        LogPassthroughElementSkipped(this.logger, elementId);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Passthrough regions: requested={RequestedCount}, clamped={ClampedCount}")]
    private static partial void LogPassthroughRegionsComputed(ILogger logger, int requestedCount, int clampedCount);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Clamped regions [{Index}]: {Region}")]
    private static partial void LogClampedRegion(ILogger logger, int index, string region);

    [Conditional("DEBUG")]
    private void LogComputedPassthroughRegions(int requestedCount, List<RectInt32> clamped)
    {
        Debug.Assert(this.IsLoaded, "logging can only be done after view is loaded");
        LogPassthroughRegionsComputed(this.logger, requestedCount, clamped.Count);

        for (var idx = 0; idx < clamped.Count; idx++)
        {
            var rr = clamped[idx];
            LogClampedRegion(this.logger, idx, string.Format(
                CultureInfo.InvariantCulture,
                "{{ X={1}, Y={2}, W={3}, H={4} }}",
                idx,
                rr.X,
                rr.Y,
                rr.Width,
                rr.Height));
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "SetRegionRects called with {SetCount} region(s)")]
    private static partial void LogPassthroughRegionsSet(ILogger logger, int setCount);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "SetRegionRects cleared (no passthrough regions)")]
    private static partial void LogPassthroughRegionsCleared(ILogger logger);

    [Conditional("DEBUG")]
    private void LogPassthroughRegionsSet(int setCount)
    {
        Debug.Assert(this.IsLoaded, "logging can only be done after view is loaded");
        if (setCount > 0)
        {
            LogPassthroughRegionsSet(this.logger, setCount);
        }
        else
        {
            LogPassthroughRegionsCleared(this.logger);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "CustomTitleBar: Layout updated (Scale: {Scale}, System Reserved: {RightInset}, Actual Width: {ActualWidth}, Actual Height: {ActualHeight})")]
    private static partial void LogCustomTitleBarLayout(ILogger logger, double scale, double rightInset, double actualWidth, double actualHeight);

    [Conditional("DEBUG")]
    private void LogCustomTitleBarLayout(double scale, double rightInset)
    {
        Debug.Assert(this.IsLoaded, "logging can only be done after view is loaded");
        LogCustomTitleBarLayout(this.logger, scale, rightInset, this.CustomTitleBar.ActualWidth, this.CustomTitleBar.ActualHeight);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "CustomTitleBar: reserved width unchanged (prev={PrevWidth}, new={NewWidth})")]
    private static partial void LogSystemReservedWidthUnchanged(ILogger logger, double prevWidth, double newWidth);

    [Conditional("DEBUG")]
    private void LogSystemReservedWidthUnchanged(double preWidth, double newWidth)
    {
        Debug.Assert(this.IsLoaded, "logging can only be done after view is loaded");
        LogSystemReservedWidthUnchanged(this.logger, preWidth, newWidth);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "ConfigurePassthroughRegions: regions identical - skipping SetRegionRects")]
    private static partial void LogConfigurePassthroughRegionsRegionsIdentical(ILogger logger);

    [Conditional("DEBUG")]
    private void LogPassthroughRegionsIdentical()
    {
        Debug.Assert(this.IsLoaded, "logging can only be done after view is loaded");
        LogConfigurePassthroughRegionsRegionsIdentical(this.logger);
    }

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "SecondaryCommands -> {Visibility}; was visible={Currently}, window={WindowWidth:F2}, required={Required:F2}")]
    private static partial void LogSecondaryCommandsInfo(ILogger logger, Visibility visibility, bool currently, double windowWidth, double required);

    [Conditional("DEBUG")]
    private void LogSecondaryCommandsInfo(Visibility visibility, bool currentlyVisible, double windowWidth, double requiredWidth)
    {
        Debug.Assert(this.IsLoaded, "logging can only be done after view is loaded");
        LogSecondaryCommandsInfo(this.logger, visibility, currentlyVisible, windowWidth, requiredWidth);
    }
}
