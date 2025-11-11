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
        Level = LogLevel.Error,
        Message = "Cannot initiate a new drag when one is already ongoing for item '{Item}' in TabStrip '{TabStripName}'.")]
    private static partial void LogDragOngoing(ILogger logger, string tabStripName, string item);

    private void LogDragOngoing()
    {
        Debug.Assert(this.context is not null, "Context must be set during active drag");
        LogDragOngoing(this.logger, this.context.TabStrip?.Name ?? "<unknown>", GetDraggedItemName(this.context));
    }

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
    private static partial void LogDragCompletedWithDrop(ILogger logger, string item, int dragIndex, int dropIndex);

    [Conditional("DEBUG")]
    private void LogDragCompletedWithDrop(int dragIndex, int dropIndex)
        => LogDragCompletedWithDrop(this.logger, GetDraggedItemName(this.context), dragIndex, dropIndex);

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

    private static string GetDraggedItemName(DragContext? context) => context?.DraggedItemData.ToString() ?? "<null>";

    private sealed partial class ReorderLayout
    {
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
            => LogUpdateStep(this.logger, steps, this.hotspotOffset);

        [LoggerMessage(
            Level = LogLevel.Trace,
            Message = "Overlap detected with closest item {ItemIndex}. Setting as new drop target.")]
        private static partial void LogOverlapDetected(ILogger logger, int itemIndex);

        [LoggerMessage(
            Level = LogLevel.Trace,
            Message = "ResolveGapVisualIndex: GapLeft={GapLeft}, SlotOrigins={SlotOrigins}, ResolvedIndex={ResolvedIndex}")]
        private static partial void LogResolveGapVisualIndex(ILogger logger, double gapLeft, string slotOrigins, int resolvedIndex);

        [Conditional("DEBUG")]
        private void LogResolveGapVisualIndex(double gapLeft, double[] slotOrigins, int resolvedIndex)
            => LogResolveGapVisualIndex(this.logger, gapLeft, string.Join(',', slotOrigins), resolvedIndex);

        [LoggerMessage(
            Level = LogLevel.Debug,
            Message = "ReorderLayout initialized: ItemCount={ItemCount}, DraggedContentId={DraggedContentId}, DraggedVisualIndex={DraggedVisualIndex}")]
        private static partial void LogReorderLayoutInitialized(ILogger logger, int itemCount, string draggedContentId, int draggedVisualIndex);

        [Conditional("DEBUG")]
        private void LogReorderLayoutInitialized(int itemCount, string draggedContentId, int draggedVisualIndex)
            => LogReorderLayoutInitialized(this.logger, itemCount, draggedContentId, draggedVisualIndex);

        [Conditional("DEBUG")]
        private void LogOverlapDetected(int itemIndex)
            => LogOverlapDetected(this.logger, itemIndex);
    }
}
