// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using DroidNet.Coordinates;
using Microsoft.Extensions.Logging;

namespace DroidNet.Aura.Drag;

/// <summary>
///     Logging methods for the ReorderStrategy.
/// </summary>
internal partial class ReorderStrategy
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "{StrategyName} created.")]
    private static partial void LogCreated(ILogger logger, string strategyName);

    private void LogCreated() => LogCreated(this.logger, nameof(ReorderStrategy));

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Enter reorder mode at screen point ({ScreenPos})")]
    private static partial void LogEnterReorderMode(ILogger logger, SpatialPoint<PhysicalScreenSpace> screenPos);

    [Conditional("DEBUG")]
    private void LogEnterReorderMode(SpatialPoint<PhysicalScreenSpace> point)
        => LogEnterReorderMode(this.logger, point);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Drop item '{item}' in reorder mode: dragIndex={DragIndex}, dropIndex={DropIndex}")]
    private static partial void LogDrop(ILogger logger, string item, int dragIndex, int dropIndex);

    [Conditional("DEBUG")]
    private void LogDrop(int dragIndex, int dropIndex)
        => LogDrop(this.logger, GetDraggedItemName(this.context), dragIndex, dropIndex);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Reorder drag for item '{Item}' finished with no drop")]
    private static partial void LogDragCompletedNoDrop(ILogger logger, string item);

    [Conditional("DEBUG")]
    private void LogDragCompletedNoDrop()
        => LogDragCompletedNoDrop(this.logger, GetDraggedItemName(this.context));

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Item displaced: index={ItemIndex}, direction={Direction}")]
    private static partial void LogItemDisplaced(ILogger logger, int itemIndex, string direction);

    [Conditional("DEBUG")]
    private void LogItemDisplaced(int itemIndex, string direction)
        => LogItemDisplaced(this.logger, itemIndex, direction);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "{What} ignored: {Reason}")]
    private static partial void LogIgnored(ILogger logger, string what, string reason);

    [Conditional("DEBUG")]
    private void LogIgnored(string what, string reason)
        => LogIgnored(this.logger, what, reason);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Item '{Item}' successfully dropped at final index {Index}")]
    private static partial void LogDropSuccess(ILogger logger, string item, int index);

    [Conditional("DEBUG")]
    private void LogDropSuccess(int dropIndex)
        => LogDropSuccess(this.logger, GetDraggedItemName(this.context), dropIndex);

    private static string GetDraggedItemName(DragContext? context) => context?.DraggedItemData.ToString() ?? "<null>";

    private sealed partial class ReorderLayout
    {
        [LoggerMessage(
            Level = LogLevel.Trace,
            Message = "Dragged item '{Item}' translated to offset {Offset}.")]
        private static partial void LogDraggedItemTranslated(ILogger logger, string item, double offset);

        [Conditional("DEBUG")]
        private void LogDraggedItemTranslated(double offset)
            => LogDraggedItemTranslated(this.logger, GetDraggedItemName(this.context), offset);

        [LoggerMessage(
            Level = LogLevel.Trace,
            Message = "Adjacent Item {ItemIndex} displaced {Direction} and is now at offset {NewOffset}")]
        private static partial void LogAdjacentItemDisplaced(ILogger logger, int itemIndex, Direction direction, double newOffset);

        [Conditional("DEBUG")]
        private void LogAdjacentItemDisplaced(int itemIndex, Direction direction, double newOffset)
            => LogAdjacentItemDisplaced(this.logger, itemIndex, direction, newOffset);

        [LoggerMessage(
            Level = LogLevel.Trace,
            Message = "Snapshot[{Index}]: ItemIndex={ItemIndex}, LayoutOrigin={LayoutOrigin}, Width={Width}")]
        private static partial void LogSnapshot(ILogger logger, int index, int itemIndex, double layoutOrigin, double width);

        [Conditional("DEBUG")]
        private void LogSnapshot(int index, TabStripItemSnapshot item)
            => LogSnapshot(this.logger, index, item.ItemIndex, item.LayoutOrigin.Point.X, item.Width);

        [LoggerMessage(
            Level = LogLevel.Trace,
            Message = "Found dragged item at visual index {VisualIndex}, with grab offset X {GrabOffsetX}")]
        private static partial void LogDraggedItemFound(ILogger logger, int visualIndex, double grabOffsetX);

        [Conditional("DEBUG")]
        private void LogDraggedItemFound(int visualIndex, double grabOffsetX)
            => LogDraggedItemFound(this.logger, visualIndex, grabOffsetX);

        [LoggerMessage(
            Level = LogLevel.Trace,
            Message = "Not updating reorder layout; pointer ({PointerX}) too far to the {Direction}.")]
        private static partial void LogDragUpdateSkipped(ILogger logger, double pointerX, string direction);

        [Conditional("DEBUG")]
        private void LogDragUpdateSkipped(double pointerX, Direction direction)
            => LogDragUpdateSkipped(this.logger, pointerX, direction == Direction.Left ? "left" : "right");

        [LoggerMessage(
            Level = LogLevel.Trace,
            Message = "Updating reorder layout: Pointer=(From={PointerStart}, To={PointerTo}), Delta={Delta}, Direction={Direction}.")]
        private static partial void LogDragUpdate(ILogger logger, double pointerStart, double pointerTo, double delta, Direction direction);

        [Conditional("DEBUG")]
        private void LogDragUpdate(double pointerStart, double pointerTo, double delta, Direction direction)
            => LogDragUpdate(this.logger, pointerStart, pointerTo, delta, direction);

        [LoggerMessage(
            Level = LogLevel.Trace,
            Message = "Reorder layout update completed in {Steps} steps. Current hotspot is at {LastPointerX}")]
        private static partial void LogUpdateStep(ILogger logger, int steps, double lastPointerX);

        [Conditional("DEBUG")]
        private void LogDragUpdateComplete(int steps)
            => LogUpdateStep(this.logger, steps, this.currentHotspotX);

        [LoggerMessage(
            Level = LogLevel.Trace,
            Message = "Overlap detected with closest item {ItemIndex}. Setting as new drop target.")]
        private static partial void LogOverlapDetected(ILogger logger, int itemIndex);

        [Conditional("DEBUG")]
        private void LogOverlapDetected(int itemIndex)
            => LogOverlapDetected(this.logger, itemIndex);
    }
}
