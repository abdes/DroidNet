// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace DroidNet.Aura.Drag;

/// <summary>
///     Application-wide drag coordinator that maintains active drag state across all <see
///     cref="ITabStrip"/> instances within the process. It serializes drag lifecycle operations
///     (start/move/end) and drives the <see cref="IDragVisualService"/>.
/// </summary>
public partial class TabDragCoordinator
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "{ServiceName} created.")]
    private static partial void LogCreated(ILogger logger, string serviceName);

    private void LogCreated() => LogCreated(this.logger, nameof(TabDragCoordinator));

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Drag started: Item={ItemContent}, Source={SourceStripName}, InitialCursorX={InitialCursorX}, InitialCursorY={InitialCursorY}")]
    private static partial void LogDragStarted(
        ILogger logger,
        string itemContent,
        string sourceStripName,
        int initialCursorX,
        int initialCursorY);

    [Conditional("DEBUG")]
    private void LogDragStarted()
    {
        if (this.dragContext is not { TabStrip: { } strip } context)
        {
            return;
        }

        var sourceStripName = strip.GetType().Name;
        LogDragStarted(
            this.logger,
            context.DraggedItem.ToString() ?? "Unknown",
            sourceStripName,
            (int)this.lastCursorPosition.Point.X,
            (int)this.lastCursorPosition.Point.Y);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Attempt to start drag failed: Drag already active")]
    private static partial void LogDragAlreadyActive(ILogger logger);

    private void LogDragAlreadyActive() => LogDragAlreadyActive(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Drag move: CursorX={CursorX}, CursorY={CursorY}, Delta=({DeltaX}, {DeltaY})")]
    private static partial void LogDragMoved(
        ILogger logger,
        int cursorX,
        int cursorY,
        double deltaX,
        double deltaY);

    private void LogDragMoved(Windows.Foundation.Point screenPoint, Windows.Foundation.Point previousPosition)
    {
        if (!this.logger.IsEnabled(LogLevel.Trace))
        {
            return;
        }

        var deltaX = screenPoint.X - previousPosition.X;
        var deltaY = screenPoint.Y - previousPosition.Y;
        LogDragMoved(
            this.logger,
            (int)screenPoint.X,
            (int)screenPoint.Y,
            deltaX,
            deltaY);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Drag move ignored: Not active or no session token")]
    private static partial void LogDragMoveIgnored(ILogger logger);

    private void LogDragMoveIgnored()
    {
        if (!this.logger.IsEnabled(LogLevel.Trace))
        {
            return;
        }

        LogDragMoveIgnored(this.logger);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Drag move ignored: Position unchanged (less than 0.5px movement)")]
    private static partial void LogDragMoveIgnoredNoChange(ILogger logger);

    private void LogDragMoveIgnoredNoChange()
    {
        if (!this.logger.IsEnabled(LogLevel.Trace))
        {
            return;
        }

        LogDragMoveIgnoredNoChange(this.logger);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Drag ended: ScreenX={ScreenX}, ScreenY={ScreenY}, DroppedOverStrip={DroppedOverStrip}, Destination={DestinationStripName}, NewIndex={NewIndex}")]
    private static partial void LogDragEnded(
        ILogger logger,
        int screenX,
        int screenY,
        bool droppedOverStrip,
        string destinationStripName,
        int? newIndex);

    private void LogDragEnded(Windows.Foundation.Point screenPoint, bool droppedOverStrip, ITabStrip? destination, int? newIndex)
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        var destinationName = destination?.GetType().Name ?? "None";
        LogDragEnded(
            this.logger,
            (int)screenPoint.X,
            (int)screenPoint.Y,
            droppedOverStrip,
            destinationName,
            newIndex);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Drag ended ignored: Not active")]
    private static partial void LogDragEndedIgnored(ILogger logger);

    private void LogDragEndedIgnored()
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        LogDragEndedIgnored(this.logger);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Drag aborted")]
    private static partial void LogDragAborted(ILogger logger);

    private void LogDragAborted()
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        LogDragAborted(this.logger);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Drag abort ignored: Not active")]
    private static partial void LogDragAbortIgnored(ILogger logger);

    private void LogDragAbortIgnored()
    {
        if (!this.logger.IsEnabled(LogLevel.Trace))
        {
            return;
        }

        LogDragAbortIgnored(this.logger);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Polling timer started: Interval=16.67ms (~60Hz)")]
    private static partial void LogPollingTimerStarted(ILogger logger);

    private void LogPollingTimerStarted()
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        LogPollingTimerStarted(this.logger);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Polling timer stopped")]
    private static partial void LogPollingTimerStopped(ILogger logger);

    private void LogPollingTimerStopped()
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        LogPollingTimerStopped(this.logger);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Cannot start polling timer: No DispatcherQueue available")]
    private static partial void LogPollingTimerNoDispatcher(ILogger logger);

    private void LogPollingTimerNoDispatcher() => LogPollingTimerNoDispatcher(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Polling timer tick: Not active or no session token")]
    private static partial void LogPollingTimerTickIgnored(ILogger logger);

    private void LogPollingTimerTickIgnored()
    {
        if (!this.logger.IsEnabled(LogLevel.Trace))
        {
            return;
        }

        LogPollingTimerTickIgnored(this.logger);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Polling timer tick: Failed to get cursor position")]
    private static partial void LogGetCursorPosFailed(ILogger logger);

    private void LogGetCursorPosFailed() => LogGetCursorPosFailed(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "TabCloseRequested handler failed during transition to TearOut mode")]
    private static partial void LogTabCloseRequestedFailed(ILogger logger, Exception Exception);

    private void LogTabCloseRequestedFailed(Exception exception) => LogTabCloseRequestedFailed(this.logger, exception);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "TearOut threshold check: Cursor=({CursorX}, {CursorY}), StripBounds=[({Left}, {Top}), ({Right}, {Bottom})], Threshold={Threshold}, IsWithin={IsWithin}")]
    private partial void LogTearOutThresholdCheck(
        double cursorX,
        double cursorY,
        double left,
        double top,
        double right,
        double bottom,
        double threshold,
        bool isWithin);
}
