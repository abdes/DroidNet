// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace DroidNet.Aura.Drag;

/// <summary>
///     Strategy for handling cross-window drag operations using DragVisualService overlay.
///     This strategy manages TearOut mode with floating overlay visuals and cross-window coordination.
/// </summary>
internal sealed partial class TearOutStrategy
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "{StrategyName} created.")]
    private static partial void LogCreated(ILogger logger, string strategyName);

    private void LogCreated() => LogCreated(this.logger, nameof(TearOutStrategy));

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Enter tear-out mode at screen point ({X}, {Y})")]
    private partial void LogEnterTearOutMode(double x, double y);

    private void LogEnterTearOutMode(Windows.Foundation.Point point) =>
        this.LogEnterTearOutMode(point.X, point.Y);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Reorder drag for item '{Item}' finished with no drop")]
    private static partial void LogDragCompletedNoDrop(ILogger logger, string item);

    [Conditional("DEBUG")]
    private void LogDragCompletedNoDrop()
        => LogDragCompletedNoDrop(this.logger, this.GetDraggedItemName());

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Exit tear-out mode")]
    private partial void LogExitTearOutMode();

    [LoggerMessage(
        Level = LogLevel.Trace,
        Message = "Move in tear-out mode to ({X}, {Y})")]
    private partial void LogMove(double x, double y);

    private void LogMove(Windows.Foundation.Point point) =>
        this.LogMove(point.X, point.Y);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Drop in tear-out mode at ({X}, {Y}), target={TargetName}, index={Index}")]
    private partial void LogDrop(double x, double y, string? targetName, int? index);

    private void LogDrop(Windows.Foundation.Point point, ITabStrip? target, int? index) =>
        this.LogDrop(point.X, point.Y, target?.Name ?? "null", index);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Drop on TabStrip: {TargetName} at index {Index}")]
    private partial void LogDropOnTabStrip(string targetName, int index);

    private void LogDropOnTabStrip(ITabStrip target, int index) =>
        this.LogDropOnTabStrip(target.Name ?? "Unknown", index);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Drop outside any TabStrip at ({X}, {Y})")]
    private partial void LogDropOutside(double x, double y);

    private void LogDropOutside(Windows.Foundation.Point point) =>
        this.LogDropOutside(point.X, point.Y);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Header capture successful")]
    private partial void LogHeaderCaptureSuccess();

    [LoggerMessage(
        Level = LogLevel.Warning,
        Message = "Header capture failed: {Reason}")]
    private partial void LogHeaderCaptureFailed(string reason);

    [LoggerMessage(
        Level = LogLevel.Information,
        Message = "Design mode detected. Tear-out overlay suppressed.")]
    private partial void LogDesignModeSuppressed();

    [LoggerMessage(
        Level = LogLevel.Error,
        Message = "Header capture exception: {Message}")]
    private partial void LogHeaderCaptureException(string message);

    private void LogHeaderCaptureException(Exception ex) =>
        this.LogHeaderCaptureException(ex.Message);

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Preview image requested")]
    private partial void LogPreviewImageRequested();

    [LoggerMessage(
        Level = LogLevel.Error,
        Message = "Preview image request exception: {Message}")]
    private partial void LogPreviewImageException(string message);

    private void LogPreviewImageException(Exception ex) =>
        this.LogPreviewImageException(ex.Message);

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
        Message = "Exit ignored - strategy not active")]
    private partial void LogExitIgnored();

    [LoggerMessage(
        Level = LogLevel.Debug,
        Message = "Drop ignored - strategy not active")]
    private partial void LogDropIgnored();

    private string GetDraggedItemName() => this.context?.DraggedItemData.ToString() ?? "<null>";
}
