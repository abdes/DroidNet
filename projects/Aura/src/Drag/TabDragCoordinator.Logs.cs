// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Globalization;
using DroidNet.Coordinates;
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
        Message = "Drag started: Item={ItemContent}, Source={SourceStripName}, InitialCursorX={InitialCursorX}, InitialCursorY={InitialCursorY}, HotSpot=({HotspotX}, {HotspotY})")]
    private static partial void LogDragStarted(
        ILogger logger,
        string itemContent,
        string sourceStripName,
        int initialCursorX,
        int initialCursorY,
        int hotspotX,
        int hotspotY);

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
            context.DraggedItemData.ToString() ?? "Unknown",
            sourceStripName,
            (int)this.lastCursorPosition.Point.X,
            (int)this.lastCursorPosition.Point.Y,
            (int)this.dragContext.HotspotOffsets.X,
            (int)this.dragContext.HotspotOffsets.Y);
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
        Message = "Drag move: {Pointer}, Delta=({DeltaX}, {DeltaY})")]
    private static partial void LogDragMoved(
        ILogger logger,
        SpatialPoint<PhysicalScreenSpace> pointer,
        double deltaX,
        double deltaY);

    private void LogDragMoved(SpatialPoint<PhysicalScreenSpace> pointer, Windows.Foundation.Point previousPosition)
    {
        if (!this.logger.IsEnabled(LogLevel.Trace))
        {
            return;
        }

        var deltaX = pointer.Point.X - previousPosition.X;
        var deltaY = pointer.Point.Y - previousPosition.Y;
        LogDragMoved(this.logger, pointer, deltaX, deltaY);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Registered strip {StripName} is not a FrameworkElement and cannot be hit-tested.")]
    private static partial void LogStripNotFrameworkElement(ILogger logger, string stripName);

    private void LogStripNotFrameworkElement(ITabStrip strip)
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        LogStripNotFrameworkElement(this.logger, strip.GetType().Name);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Failed to create spatial mapper for strip {StripName}.")]
    private static partial void LogSpatialMapperCreationFailed(ILogger logger, string stripName, Exception exception);

    private void LogSpatialMapperCreationFailed(ITabStrip strip, Exception exception)
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        LogSpatialMapperCreationFailed(this.logger, strip.GetType().Name, exception);
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
        Message = "Drag ended at {Point}, DroppedOverStrip={DroppedOverStrip}, Destination={DestinationStripName}, NewIndex={NewIndex}")]
    private static partial void LogDragEnded(
        ILogger logger,
        SpatialPoint<ScreenSpace> point,
        bool droppedOverStrip,
        string destinationStripName,
        int? newIndex);

    private void LogDragEnded(SpatialPoint<ScreenSpace> point, bool droppedOverStrip, ITabStrip? destination, int? newIndex)
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        var destinationName = destination?.GetType().Name ?? "None";
        LogDragEnded(this.logger, point, droppedOverStrip, destinationName, newIndex);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "EndDrag prep: TearOut={IsTearOutMode}, HitStrip={HitStripName}, FallbackIndex={FallbackIndex}, ContextIndex={ContextIndex}")]
    private static partial void LogEndDragPreparation(
        ILogger logger,
        bool isTearOutMode,
        string hitStripName,
        int fallbackIndex,
        int contextIndex);

    private void LogEndDragPreparation(bool isTearOutMode, ITabStrip? hitStrip, int fallbackIndex, DragContext context)
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        var stripName = hitStrip?.GetType().Name ?? "None";
        LogEndDragPreparation(this.logger, isTearOutMode, stripName, fallbackIndex, GetDraggedItemIndex(context));
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Strategy complete: Type={StrategyType}, Result={Result}")]
    private static partial void LogStrategyCompletion(ILogger logger, string strategyType, string result);

    private void LogStrategyCompletion(IDragStrategy strategy, int? result)
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        var resultText = result.HasValue
            ? result.Value.ToString(CultureInfo.InvariantCulture)
            : "null";
        var strategyType = strategy.GetType().Name;
        LogStrategyCompletion(this.logger, strategyType, resultText);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Using fallback drop index {Index}")]
    private static partial void LogFallbackIndex(ILogger logger, int index);

    private void LogFallbackIndex(int index)
    {
        if (!this.logger.IsEnabled(LogLevel.Debug))
        {
            return;
        }

        LogFallbackIndex(this.logger, index);
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

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Tear-out start: item='{DraggedItem}', index={DraggedItemIndex}, strip='{StripName}'")]
    private static partial void LogTearOutStart(ILogger logger, string DraggedItem, int DraggedItemIndex, string StripName);

    private void LogTearOutStart(string draggedItem, int draggedItemIndex, string stripName)
    {
        if (this.logger is ILogger logger)
        {
            LogTearOutStart(logger, draggedItem, draggedItemIndex, stripName);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Tear-out CloseTab issued for item='{DraggedItem}'")]
    private static partial void LogTearOutCloseTab(ILogger logger, string DraggedItem);

    private void LogTearOutCloseTab(string draggedItem)
    {
        if (this.logger is ILogger logger)
        {
            LogTearOutCloseTab(logger, draggedItem);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Tear-out RemoveItemAt({DraggedItemIndex}) succeeded")]
    private static partial void LogTearOutRemoveItemSuccess(ILogger logger, int DraggedItemIndex);

    private void LogTearOutRemoveItemSuccess(int draggedItemIndex)
    {
        if (this.logger is ILogger logger)
        {
            LogTearOutRemoveItemSuccess(logger, draggedItemIndex);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Tear-out RemoveItemAt skipped because index {DraggedItemIndex} is out of range")]
    private static partial void LogTearOutRemoveItemSkipped(ILogger logger, int DraggedItemIndex);

    private void LogTearOutRemoveItemSkipped(int draggedItemIndex)
    {
        if (this.logger is ILogger logger)
        {
            LogTearOutRemoveItemSkipped(logger, draggedItemIndex);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Tear-out failed")]
    private static partial void LogTearOutFailed(ILogger logger, Exception Exception);

    private void LogTearOutFailed(Exception exception)
    {
        if (this.logger is ILogger logger)
        {
            LogTearOutFailed(logger, exception);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Tear-out context reset for item='{DraggedItem}'")]
    private static partial void LogTearOutContextReset(ILogger logger, string DraggedItem);

    private void LogTearOutContextReset(string draggedItem)
    {
        if (this.logger is ILogger logger)
        {
            LogTearOutContextReset(logger, draggedItem);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "AttachToStrip aborted: container has no DispatcherQueue.")]
    private static partial void LogAttachToStripAborted(ILogger logger);

    private void LogAttachToStripAborted()
    {
        if (this.logger is ILogger logger)
        {
            LogAttachToStripAborted(logger);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "AttachToStrip failed to enqueue preparation on UI thread.")]
    private static partial void LogAttachToStripEnqueueFailed(ILogger logger);

    private void LogAttachToStripEnqueueFailed()
    {
        if (this.logger is ILogger logger)
        {
            LogAttachToStripEnqueueFailed(logger);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "AttachToStrip cancelled during preparation.")]
    private static partial void LogAttachToStripCancelled(ILogger logger);

    private void LogAttachToStripCancelled()
    {
        if (this.logger is ILogger logger)
        {
            LogAttachToStripCancelled(logger);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "AttachToStrip failed")]
    private static partial void LogAttachToStripFailed(ILogger logger, Exception? ex);

    private void LogAttachToStripFailed(Exception? ex)
    {
        if (this.logger is ILogger logger)
        {
            LogAttachToStripFailed(logger, ex);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to enqueue asuync operations on DispatcherQueue while executing '{Operation}'.")]
    private static partial void LogDispatcherEnqueueFailed(ILogger logger, string operation);

    private void LogDispatcherEnqueueFailed(string operation)
    {
        if (this.logger is ILogger logger)
        {
            LogDispatcherEnqueueFailed(logger, operation);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "AttachToStrip succeeded: strip='{StripName}', index={DropIndex}")]
    private static partial void LogAttachToStripSucceeded(ILogger logger, string StripName, int DropIndex);

    private void LogAttachToStripSucceeded(string stripName, int dropIndex)
    {
        if (this.logger is ILogger logger)
        {
            LogAttachToStripSucceeded(logger, stripName, dropIndex);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Detaching pending attachment from strip='{StripName}', index={DraggedItemIndex}")]
    private static partial void LogDetachPendingAttachment(ILogger logger, string StripName, int? DraggedItemIndex);

    private void LogDetachPendingAttachment(string stripName, int? draggedItemIndex)
    {
        if (this.logger is ILogger logger)
        {
            LogDetachPendingAttachment(logger, stripName, draggedItemIndex);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Cancelled pending insert for ContentId={ContentId}")]
    private static partial void LogCancelledPendingInsert(ILogger logger, Guid ContentId);

    private void LogCancelledPendingInsert(Guid contentId)
    {
        if (this.logger is ILogger logger)
        {
            LogCancelledPendingInsert(logger, contentId);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Exception in enqueded operation during OnDragPositionChanged handling.")]
    private static partial void LogOnDragPositionChangedError(ILogger logger, Exception ex);

    private void LogOnDragPositionChangedError(Exception ex)
    {
        if (this.logger is ILogger logger)
        {
            LogOnDragPositionChangedError(logger, ex);
        }
    }
}
