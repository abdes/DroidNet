// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace DroidNet.Controls;

/// <summary>
///     Logging methods for the stack-based ReorderStrategy.
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
        Message = "Enter reorder mode at strip point ({X}, {Y})")]
    private partial void LogEnterReorderMode(double x, double y);

    private void LogEnterReorderMode(Windows.Foundation.Point point) =>
        this.LogEnterReorderMode(point.X, point.Y);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Exit reorder mode")]
    private partial void LogExitReorderMode();

    [LoggerMessage(
        Level = LogLevel.Trace,
        Message = "Move in reorder mode: strip=({X}, {Y})")]
    private partial void LogMove(double x, double y);

    private void LogMove(Windows.Foundation.Point stripPoint) =>
        this.LogMove(stripPoint.X, stripPoint.Y);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Drop in reorder mode at ({X}, {Y}), target={TargetName}, index={Index}")]
    private partial void LogDrop(double x, double y, string? targetName, int? index);

    private void LogDrop(Windows.Foundation.Point point, TabStrip? target, int? index) =>
        this.LogDrop(point.X, point.Y, target?.Name ?? "null", index);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Item pushed onto stack: index={ItemIndex}, direction={Direction}")]
    private partial void LogItemPushed(int itemIndex, PushDirection direction);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Item popped from stack: index={ItemIndex}")]
    private partial void LogItemPopped(int itemIndex);

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
    private partial void LogDropSuccess(int? index);
}
