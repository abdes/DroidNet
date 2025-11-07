// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;

namespace DroidNet.Aura.Controls;

/// <summary>
///     Implementation of the TabStrip layout manager.
/// </summary>
public partial class TabStripLayoutManager
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "ComputeCompact: availableWidth={AvailableWidth}, pinnedCount={PinnedCount}, regularCount={RegularCount}")]
    private static partial void LogComputeCompact(ILogger logger, double AvailableWidth, int PinnedCount, int RegularCount);

    [Conditional("DEBUG")]
    private void LogComputeCompact(double availableWidth, int pinnedCount, int regularCount)
        => LogComputeCompact(this.logger!, availableWidth, pinnedCount, regularCount);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Pinned item: idx={Index}, min={MinEffective}, desired={Desired}, width={Width}")]
    private static partial void LogPinnedItem(ILogger logger, int Index, double MinEffective, double Desired, double Width);

    [Conditional("DEBUG")]
    private void LogPinnedItem(int index, double minEffective, double desired, double width)
        => LogPinnedItem(this.logger!, index, minEffective, desired, width);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "ComputeCompact result: sumPinned={SumPinned}, sumRegular={SumRegular}, needsScrolling={NeedsScrolling}")]
    private static partial void LogComputeCompactResult(ILogger logger, double SumPinned, double SumRegular, bool NeedsScrolling);

    [Conditional("DEBUG")]
    private void LogComputeCompactResult(double sumPinned, double sumRegular, bool needsScrolling)
        => LogComputeCompactResult(this.logger!, sumPinned, sumRegular, needsScrolling);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "ComputeCompactRegular: availableWidth={AvailableWidth}, itemCount={ItemCount}")]
    private static partial void LogComputeCompactRegularStart(ILogger logger, double AvailableWidth, int ItemCount);

    [Conditional("DEBUG")]
    private void LogComputeCompactRegularStart(double availableWidth, int itemCount)
        => LogComputeCompactRegularStart(this.logger!, availableWidth, itemCount);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Regular item desired: idx={Index}, min={MinEffective}, desired={Desired}, clampedDesired={Clamped}")]
    private static partial void LogRegularItemDesired(ILogger logger, int Index, double MinEffective, double Desired, double Clamped);

    [Conditional("DEBUG")]
    private void LogRegularItemDesired(int index, double minEffective, double desired, double clamped)
        => LogRegularItemDesired(this.logger!, index, minEffective, desired, clamped);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "ComputeCompactRegular sumDesired={SumDesired}")]
    private static partial void LogComputeCompactRegularSumDesired(ILogger logger, double SumDesired);

    [Conditional("DEBUG")]
    private void LogComputeCompactRegularSumDesired(double sumDesired)
        => LogComputeCompactRegularSumDesired(this.logger!, sumDesired);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "All items fit, no shrink needed")]
    private static partial void LogAllItemsFit(ILogger logger);

    [Conditional("DEBUG")]
    private void LogAllItemsFit()
        => LogAllItemsFit(this.logger!);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Assign item: idx={Index}, width={Width}")]
    private static partial void LogAssignItem(ILogger logger, int Index, double Width);

    [Conditional("DEBUG")]
    private void LogAssignItem(int index, double width)
        => LogAssignItem(this.logger!, index, width);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Shrinking needed: deficit={Deficit}")]
    private static partial void LogShrinkingNeeded(ILogger logger, double Deficit);

    [Conditional("DEBUG")]
    private void LogShrinkingNeeded(double deficit)
        => LogShrinkingNeeded(this.logger!, deficit);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Shrink step: idx={Index}, old={Old}, shrinkBy={ShrinkBy}, new={New}, clamped={Clamped}")]
    private static partial void LogShrinkStep(ILogger logger, int Index, double Old, double ShrinkBy, double New, double Clamped);

    [Conditional("DEBUG")]
    private void LogShrinkStep(int index, double oldValue, double shrinkBy, double newValue, double clamped)
        => LogShrinkStep(this.logger!, index, oldValue, shrinkBy, newValue, clamped);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "After shrink: newSum={NewSum}, deficit={Deficit}, remaining={Remaining}")]
    private static partial void LogAfterShrink(ILogger logger, double NewSum, double Deficit, int Remaining);

    [Conditional("DEBUG")]
    private void LogAfterShrink(double newSum, double deficit, int remaining)
        => LogAfterShrink(this.logger!, newSum, deficit, remaining);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Trace,
        Message = "Final assign: idx={Index}, width={Width}, compacted={Compacted}")]
    private static partial void LogFinalAssign(ILogger logger, int Index, double Width, bool Compacted);

    [Conditional("DEBUG")]
    private void LogFinalAssign(int index, double width, bool compacted)
        => LogFinalAssign(this.logger!, index, width, compacted);
}
