// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Coordinates;
using Microsoft.Extensions.Logging;

namespace DroidNet.Controls;

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
        Message = "Enter reorder mode at strip point ({ScreenPos}, {LocalPos})")]
    private static partial void LogEnterReorderMode(ILogger logger, SpatialPoint<ScreenSpace> screenPos, SpatialPoint<ElementSpace> localPos);

    private void LogEnterReorderMode(SpatialPoint<ScreenSpace> screenPos, SpatialPoint<ElementSpace> localPos)
        => LogEnterReorderMode(this.logger, screenPos, localPos);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Drop in reorder mode: dragIndex={DragIndex}, dropIndex={DropIndex}")]
    private partial void LogDrop(int dragIndex, int dropIndex);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Item displaced: index={ItemIndex}, direction={Direction}")]
    private partial void LogItemDisplaced(int itemIndex, string direction);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Reorder initiated: draggedItemVisualIndex={Index}")]
    private partial void LogReorderInitiated(int index);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Committed indices: drag={DragIndex}, drop={DropIndex}")]
    private partial void LogCommittedIndices(int dragIndex, int dropIndex);

    [LoggerMessage(
        Level = LogLevel.Warning,
        Message = "Strategy already active")]
    private partial void LogAlreadyActive();

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Move ignored - strategy not active")]
    private partial void LogMoveIgnored();

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Drop ignored - strategy not active")]
    private partial void LogDropIgnored();

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Drop successful at final index {Index}")]
    private partial void LogDropSuccess(int index);
}
