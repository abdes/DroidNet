// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Diagnostics;
using DroidNet.Coordinates;
using Microsoft.Extensions.Logging;

namespace DroidNet.Aura.Controls;

/// <summary>
///    A lightweight, reusable tab strip control for WinUI 3 that displays a dynamic row of tabs
///    and raises events or executes commands when tabs are invoked, selected, or closed.
/// </summary>
public partial class TabStrip
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Coordinator subscribed.")]
    private static partial void LogCoordinatorSubscribed(ILogger logger);

    [Conditional("DEBUG")]
    private void LogCoordinatorSubscribed()
    {
        if (this.logger is ILogger logger)
        {
            LogCoordinatorSubscribed(logger);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Coordinator unsubscribed.")]
    private static partial void LogCoordinatorUnsubscribed(ILogger logger);

    [Conditional("DEBUG")]
    private void LogCoordinatorUnsubscribed()
    {
        if (this.logger is ILogger logger)
        {
            LogCoordinatorUnsubscribed(logger);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Pointer pressed on item at position ({X}, {Y}).")]
    private static partial void LogPointerPressedImpl(ILogger logger, double X, double Y);

    [Conditional("DEBUG")]
    private void LogPointerPressed(TabStripItem item, Windows.Foundation.Point position)
    {
        if (this.logger is ILogger logger)
        {
            LogPointerPressedImpl(logger, position.X, position.Y);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Pointer moved to ({X}, {Y}), delta={Delta}.")]
    private static partial void LogPointerMovedImpl(ILogger logger, double X, double Y, double Delta);

    [Conditional("DEBUG")]
    private void LogPointerMoved(TabStripItem item, Windows.Foundation.Point position, double delta)
    {
        if (this.logger is ILogger logger)
        {
            LogPointerMovedImpl(logger, position.X, position.Y, delta);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Pointer released while drag is ongoing.")]
    private static partial void LogPointerReleasedWhileDragging(ILogger logger);

    [Conditional("DEBUG")]
    private void LogPointerReleasedWhileDragging()
    {
        if (this.logger is ILogger logger)
        {
            LogPointerReleasedWhileDragging(logger);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Drag threshold exceeded: delta={Delta} >= threshold={Threshold}.")]
    private static partial void LogThresholdExceeded(ILogger logger, double Delta, double Threshold);

    [Conditional("DEBUG")]
    private void LogThresholdExceeded(double delta, double threshold)
    {
        if (this.logger is ILogger logger)
        {
            LogThresholdExceeded(logger, delta, threshold);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Initiating drag for item '{Item}' inside TabStrip '{Strip}' at screen point ({Point})")]
    private static partial void LogBeginDragStartedImpl(ILogger logger, string item, string strip, SpatialPoint<ElementSpace> point);

    [Conditional("DEBUG")]
    private void LogInitiateDrag(TabItem item, SpatialPoint<ElementSpace> point)
    {
        if (this.logger is ILogger logger)
        {
            LogBeginDragStartedImpl(logger, item.Title, this.Name, point);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Selection cleared for drag.")]
    private static partial void LogSelectionClearedImpl(ILogger logger);

    [Conditional("DEBUG")]
    private void LogSelectionCleared()
    {
        if (this.logger is ILogger logger)
        {
            LogSelectionClearedImpl(logger);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to initiate drag.")]
    private static partial void LogDragSessionFailure(ILogger logger, InvalidOperationException Exception);

    [Conditional("DEBUG")]
    private void LogInitiateDragFailed(InvalidOperationException exception)
    {
        if (this.logger is ILogger logger)
        {
            LogDragSessionFailure(logger, exception);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Coordinator reported drag moved to ({X}, {Y}).")]
    private static partial void LogCoordinatorDragMovedImpl(ILogger logger, double X, double Y);

    [Conditional("DEBUG")]
    private void LogCoordinatorDragMoved(Windows.Foundation.Point screenPoint)
    {
        if (this.logger is ILogger logger)
        {
            LogCoordinatorDragMovedImpl(logger, screenPoint.X, screenPoint.Y);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Coordinator reported drag ended at ({X}, {Y}), destination hash={DestHash}, newIndex={NewIndex}.")]
    private static partial void LogCoordinatorDragEndedImpl(ILogger logger, double X, double Y, int DestHash, int NewIndex);

    [Conditional("DEBUG")]
    private void LogCoordinatorDragEnded(Windows.Foundation.Point screenPoint, TabStrip? destination, int? newIndex)
    {
        if (this.logger is ILogger logger)
        {
            LogCoordinatorDragEndedImpl(logger, screenPoint.X, screenPoint.Y, destination?.GetHashCode() ?? 0, newIndex ?? -1);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Exception during drag end.")]
    private static partial void LogDragEndException(ILogger logger, Exception Exception);

    [Conditional("DEBUG")]
    private void LogDragEndException(Exception exception)
    {
        if (this.logger is ILogger logger)
        {
            LogDragEndException(logger, exception);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Exception in TabCloseRequested handler.")]
    private static partial void LogTabCloseRequestedException(ILogger logger, Exception Exception);

    [Conditional("DEBUG")]
    private void LogTabCloseRequestedException(Exception exception)
    {
        if (this.logger is ILogger logger)
        {
            LogTabCloseRequestedException(logger, exception);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Exception in TabDragComplete handler.")]
    private static partial void LogTabDragCompleteException(ILogger logger, Exception Exception);

    [Conditional("DEBUG")]
    private void LogTabDragCompleteException(Exception exception)
    {
        if (this.logger is ILogger logger)
        {
            LogTabDragCompleteException(logger, exception);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Exception in TabTearOutRequested handler.")]
    private static partial void LogTabTearOutRequestedException(ILogger logger, Exception Exception);

    [Conditional("DEBUG")]
    private void LogTabTearOutRequestedException(Exception exception)
    {
        if (this.logger is ILogger logger)
        {
            LogTabTearOutRequestedException(logger, exception);
        }
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Drag state cleanup for '{Header}' (ContentId={ContentId}): cleared {ClearedCount} container(s).")]
    private static partial void LogDragStateCleanupImpl(ILogger logger, string Header, Guid ContentId, int ClearedCount);

    [Conditional("DEBUG")]
    private void LogDragStateCleanup(TabItem item, int clearedCount)
    {
        if (this.logger is ILogger logger)
        {
            LogDragStateCleanupImpl(logger, item.Header ?? "<null>", item.ContentId, clearedCount);
        }
    }
}
