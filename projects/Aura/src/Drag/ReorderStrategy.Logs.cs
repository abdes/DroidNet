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
internal sealed partial class ReorderStrategy
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
        => LogDrop(this.logger, this.GetDraggedItemName(), dragIndex, dropIndex);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Reorder drag for item '{Item}' finished with no drop")]
    private static partial void LogDragCompletedNoDrop(ILogger logger, string item);

    [Conditional("DEBUG")]
    private void LogDragCompletedNoDrop()
        => LogDragCompletedNoDrop(this.logger, this.GetDraggedItemName());

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
        => LogDropSuccess(this.logger, this.GetDraggedItemName(), dropIndex);

    private string GetDraggedItemName() => this.context?.DraggedItem.ToString() ?? "<null>";
}
