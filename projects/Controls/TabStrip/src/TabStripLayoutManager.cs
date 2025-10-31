// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
/// Implementation of the TabStrip layout manager.
/// </summary>
public class TabStripLayoutManager : ITabStripLayoutManager
{
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

        System.Diagnostics.Debug.WriteLine($"[LayoutManager] ComputeCompact: availableWidth={request.AvailableWidth}, pinnedCount={pinnedItems.Count}, regularCount={regularItems.Count}");

        foreach (var item in pinnedItems)
        {
            var minEffective = Math.Min(item.MinWidth, this.MaxItemWidth);
            var width = Math.Max(minEffective, Math.Min(this.MaxItemWidth, item.DesiredWidth));
            outputs.Add(new LayoutPerItemOutput(item.Index, width, true));
            sumPinned += width;
            System.Diagnostics.Debug.WriteLine($"[LayoutManager] Pinned: idx={item.Index}, min={minEffective}, desired={item.DesiredWidth}, width={width}");
        }

        // Now, compute regular widths
        var (regularOutputs, sumRegular, needsScrolling) = this.ComputeCompactRegular(regularItems.ToList(), request.AvailableWidth);
        outputs.AddRange(regularOutputs);

        System.Diagnostics.Debug.WriteLine($"[LayoutManager] ComputeCompact: sumPinned={sumPinned}, sumRegular={sumRegular}, needsScrolling={needsScrolling}");

        return new LayoutResult(outputs, sumPinned, sumRegular, needsScrolling);
    }

    private (List<LayoutPerItemOutput> outputs, double sum, bool needsScrolling) ComputeCompactRegular(
        List<LayoutPerItemInput> regularItems, double availableWidth)
    {
        var outputs = new List<LayoutPerItemOutput>();
        var desiredWidths = new List<double>();
        var minWidths = new List<double>();

        System.Diagnostics.Debug.WriteLine($"[LayoutManager] ComputeCompactRegular: availableWidth={availableWidth}, itemCount={regularItems.Count}");

        for (int i = 0; i < regularItems.Count; i++)
        {
            var item = regularItems[i];
            var minEffective = Math.Min(item.MinWidth, this.MaxItemWidth);
            var desired = Math.Max(minEffective, Math.Min(this.MaxItemWidth, item.DesiredWidth));
            desiredWidths.Add(desired);
            minWidths.Add(minEffective);
            System.Diagnostics.Debug.WriteLine($"[LayoutManager] Item idx={item.Index}, min={minEffective}, desired={item.DesiredWidth}, clampedDesired={desired}");
        }

        var sumDesired = desiredWidths.Sum();
        System.Diagnostics.Debug.WriteLine($"[LayoutManager] ComputeCompactRegular: sumDesired={sumDesired}");
        if (sumDesired <= availableWidth)
        {
            System.Diagnostics.Debug.WriteLine($"[LayoutManager] All items fit, no shrink needed.");
            for (var i = 0; i < regularItems.Count; i++)
            {
                outputs.Add(new LayoutPerItemOutput(regularItems[i].Index, desiredWidths[i], false));
                System.Diagnostics.Debug.WriteLine($"[LayoutManager] Assign idx={regularItems[i].Index}, width={desiredWidths[i]}");
            }
            return (outputs, sumDesired, false);
        }

        // Iterative shrinking
        var currentWidths = new List<double>(desiredWidths);
        var remainingIndices = Enumerable.Range(0, regularItems.Count).ToList();

        var deficit = sumDesired - availableWidth;
        System.Diagnostics.Debug.WriteLine($"[LayoutManager] Shrinking needed: deficit={deficit}");
        while (deficit > 0 && remainingIndices.Count > 0)
        {
            var shrinkPerItem = deficit / remainingIndices.Count;
            var newRemaining = new List<int>();

            foreach (var idx in remainingIndices)
            {
                var newWidth = currentWidths[idx] - shrinkPerItem;
                var clamped = Math.Max(minWidths[idx], newWidth);
                System.Diagnostics.Debug.WriteLine($"[LayoutManager] Shrink idx={regularItems[idx].Index}, old={currentWidths[idx]}, shrinkBy={shrinkPerItem}, new={newWidth}, clamped={clamped}");
                currentWidths[idx] = clamped;
                if (clamped > minWidths[idx])
                {
                    newRemaining.Add(idx);
                }
            }

            var newSum = currentWidths.Sum();
            deficit = Math.Max(0, newSum - availableWidth);
            System.Diagnostics.Debug.WriteLine($"[LayoutManager] After shrink: newSum={newSum}, deficit={deficit}, remaining={newRemaining.Count}");
            remainingIndices = newRemaining;
        }

        var finalSum = currentWidths.Sum();
        var needsScrolling = finalSum > availableWidth;

        for (var i = 0; i < regularItems.Count; i++)
        {
            var isCompact = currentWidths[i] < desiredWidths[i];
            outputs.Add(new LayoutPerItemOutput(regularItems[i].Index, currentWidths[i], false));
            System.Diagnostics.Debug.WriteLine($"[LayoutManager] Final assign idx={regularItems[i].Index}, width={currentWidths[i]}, compacted={isCompact}");
        }

        return (outputs, finalSum, needsScrolling);
    }
}
