// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

namespace DroidNet.Controls;

/// <summary>
///     Implementation of the TabStrip layout manager.
/// </summary>
public partial class TabStripLayoutManager : ITabStripLayoutManager
{
    private ILogger logger = NullLoggerFactory.Instance.CreateLogger<TabStripLayoutManager>();

    /// <summary>
    /// Gets or sets the logger factory used to create the logger for this layout manager.
    /// Setting this property will reset the internal logger to one created from the provided factory.
    /// </summary>
    public ILoggerFactory? LoggerFactory
    {
        get => field;
        set
        {
            field = value;
            this.logger = field?.CreateLogger<TabStripLayoutManager>() ?? NullLoggerFactory.Instance.CreateLogger<TabStripLayoutManager>();
        }
    }

    /// <inheritdoc />
    public double MaxItemWidth { get; set; }

    /// <inheritdoc />
    public double PreferredItemWidth { get; set; }

    /// <inheritdoc />
    public TabWidthPolicy Policy { get; set; }

    /// <inheritdoc />
    public LayoutResult ComputeLayout(LayoutRequest request)
    {
        var effectivePreferred = this.PreferredItemWidth > this.MaxItemWidth ? this.MaxItemWidth : this.PreferredItemWidth;

        return this.Policy switch
        {
            TabWidthPolicy.Auto => this.ComputeAuto(request),
            TabWidthPolicy.Equal => this.ComputeEqual(request, effectivePreferred),
            TabWidthPolicy.Compact => this.ComputeCompact(request),
            _ => throw new InvalidOperationException($"Unsupported TabWidthPolicy: {this.Policy}"),
        };
    }

    private LayoutResult ComputeAuto(LayoutRequest request)
    {
        var outputs = new List<LayoutPerItemOutput>();
        double sumPinned = 0;
        double sumRegular = 0;

        foreach (var item in request.Items)
        {
            var minEffective = Math.Min(item.MinWidth, this.MaxItemWidth);
            var width = Math.Max(minEffective, Math.Min(this.MaxItemWidth, item.DesiredWidth));

            outputs.Add(new LayoutPerItemOutput(item.Index, width, item.IsPinned));

            if (item.IsPinned)
            {
                sumPinned += width;
            }
            else
            {
                sumRegular += width;
            }
        }

        var needsScrolling = sumRegular > request.AvailableWidth;
        return new LayoutResult(outputs, sumPinned, sumRegular, needsScrolling);
    }

    private LayoutResult ComputeEqual(LayoutRequest request, double effectivePreferred)
    {
        var outputs = new List<LayoutPerItemOutput>();
        double sumPinned = 0;
        double sumRegular = 0;

        foreach (var item in request.Items)
        {
            var minEffective = Math.Min(item.MinWidth, this.MaxItemWidth);
            var width = Math.Max(minEffective, Math.Min(this.MaxItemWidth, effectivePreferred));

            outputs.Add(new LayoutPerItemOutput(item.Index, width, item.IsPinned));

            if (item.IsPinned)
            {
                sumPinned += width;
            }
            else
            {
                sumRegular += width;
            }
        }

        var needsScrolling = sumRegular > request.AvailableWidth;
        return new LayoutResult(outputs, sumPinned, sumRegular, needsScrolling);
    }

    private LayoutResult ComputeCompact(LayoutRequest request)
    {
        var outputs = new List<LayoutPerItemOutput>();
        double sumPinned = 0;

        // First, compute pinned widths (Auto)
        var pinnedItems = request.Items.Where(i => i.IsPinned).ToList();
        var regularItems = request.Items.Where(i => !i.IsPinned).ToList();
        this.LogComputeCompact(request.AvailableWidth, pinnedItems.Count, regularItems.Count);

        foreach (var item in pinnedItems)
        {
            var minEffective = Math.Min(item.MinWidth, this.MaxItemWidth);
            var width = Math.Max(minEffective, Math.Min(this.MaxItemWidth, item.DesiredWidth));
            outputs.Add(new LayoutPerItemOutput(item.Index, width, IsPinned: true));
            sumPinned += width;
            this.LogPinnedItem(item.Index, minEffective, item.DesiredWidth, width);
        }

        // Now, compute regular widths
        var (regularOutputs, sumRegular, needsScrolling)
            = this.ComputeCompactRegular([.. regularItems], request.AvailableWidth);
        outputs.AddRange(regularOutputs);

        this.LogComputeCompactResult(sumPinned, sumRegular, needsScrolling);

        return new LayoutResult(outputs, sumPinned, sumRegular, needsScrolling);
    }

    private (List<LayoutPerItemOutput> outputs, double sum, bool needsScrolling) ComputeCompactRegular(
        List<LayoutPerItemInput> regularItems, double availableWidth)
    {
        // Orchestrates measurement, optional fast-path assignment, and iterative shrinking.
        var outputs = new List<LayoutPerItemOutput>();

        this.LogComputeCompactRegularStart(availableWidth, regularItems.Count);

        this.BuildDesiredAndMinWidths(regularItems, out var desiredWidths, out var minWidths);

        if (this.TryAssignIfFits(desiredWidths, availableWidth, regularItems, outputs, out var sumDesired))
        {
            return (outputs, sumDesired, false);
        }

        var (currentWidths, finalSum) = this.ShrinkToFit(desiredWidths, minWidths, availableWidth, regularItems);

        var needsScrolling = finalSum > availableWidth;

        for (var i = 0; i < regularItems.Count; i++)
        {
            var isCompact = currentWidths[i] < desiredWidths[i];
            outputs.Add(new LayoutPerItemOutput(regularItems[i].Index, currentWidths[i], IsPinned: false));
            this.LogFinalAssign(regularItems[i].Index, currentWidths[i], isCompact);
        }

        return (outputs, finalSum, needsScrolling);
    }

    private void BuildDesiredAndMinWidths(List<LayoutPerItemInput> regularItems, out List<double> desiredWidths, out List<double> minWidths)
    {
        desiredWidths = new List<double>(regularItems.Count);
        minWidths = new List<double>(regularItems.Count);

        for (var i = 0; i < regularItems.Count; i++)
        {
            var item = regularItems[i];
            var minEffective = Math.Min(item.MinWidth, this.MaxItemWidth);
            var desired = Math.Max(minEffective, Math.Min(this.MaxItemWidth, item.DesiredWidth));
            desiredWidths.Add(desired);
            minWidths.Add(minEffective);
            this.LogRegularItemDesired(item.Index, minEffective, item.DesiredWidth, desired);
        }
    }

    private bool TryAssignIfFits(List<double> desiredWidths, double availableWidth, List<LayoutPerItemInput> regularItems, List<LayoutPerItemOutput> outputs, out double sumDesired)
    {
        sumDesired = desiredWidths.Sum();
        this.LogComputeCompactRegularSumDesired(sumDesired);

        if (sumDesired <= availableWidth)
        {
            this.LogAllItemsFit();
            for (var i = 0; i < regularItems.Count; i++)
            {
                outputs.Add(new LayoutPerItemOutput(regularItems[i].Index, desiredWidths[i], IsPinned: false));
                this.LogAssignItem(regularItems[i].Index, desiredWidths[i]);
            }

            return true;
        }

        return false;
    }

    private (List<double> currentWidths, double finalSum) ShrinkToFit(List<double> desiredWidths, List<double> minWidths, double availableWidth, List<LayoutPerItemInput> regularItems)
    {
        var currentWidths = new List<double>(desiredWidths);
        var remainingIndices = Enumerable.Range(0, regularItems.Count).ToList();

        var deficit = desiredWidths.Sum() - availableWidth;
        this.LogShrinkingNeeded(deficit);

        while (deficit > 0 && remainingIndices.Count > 0)
        {
            var shrinkPerItem = deficit / remainingIndices.Count;
            var newRemaining = new List<int>();

            foreach (var idx in remainingIndices)
            {
                var newWidth = currentWidths[idx] - shrinkPerItem;
                var clamped = Math.Max(minWidths[idx], newWidth);
                this.LogShrinkStep(regularItems[idx].Index, currentWidths[idx], shrinkPerItem, newWidth, clamped);
                currentWidths[idx] = clamped;
                if (clamped > minWidths[idx])
                {
                    newRemaining.Add(idx);
                }
            }

            var newSum = currentWidths.Sum();
            deficit = Math.Max(0, newSum - availableWidth);
            this.LogAfterShrink(newSum, deficit, newRemaining.Count);
            remainingIndices = newRemaining;
        }

        var finalSum = currentWidths.Sum();
        return (currentWidths, finalSum);
    }
}
